// $Id$

#include "CommandController.hh"
#include "WD2793.hh"


// {3,6,10,15} in ms case of of 2 MHz clock 
// double this if a 1MHz clock is used (MSX = 1MHz)
const byte WD2793::timePerStep[4] = {
	6000, 12000, 20000, 30000
};


WD2793::WD2793(MSXConfig::Device *config, const EmuTime &time)
	: FDC(config)
{
	reset(time);
}

WD2793::~WD2793()
{
}

void WD2793::reset(const EmuTime &time)
{
	PRT_DEBUG("WD2793::reset()");
	statusReg = 0;
	trackReg = 0;
	dataReg = 0;

	current_track = 0;
	current_sector = 0;
	current_side = 0;
	stepSpeed = 0;
	directionIn = true;

	current_drive = NO_DRIVE;
	for (int i = 0; i < NUM_DRIVES; i++) {
		motorStatus[i] = 0;
		motorStartTime[i] = time;
	}

	// According to the specs it nows issues a RestoreCommando (0x03)
	// Afterwards the stepping rate can still be changed so that the
	// remaining steps of the restorecommand can go faster. On an MSX this
	// time can be ignored since the bootstrap of the MSX takes MUCH longer
	// then an, even failing, Restorecommand
	commandReg = 0x03;
	sectorReg = 0x01;
	DRQ = false;
	INTRQ = false;
	//statusReg bit 7 (Not Ready status) is already reset
}

void WD2793::setDriveSelect(DriveNum drive, const EmuTime &time)
{
	current_drive = drive;
}
//actually not used, maybe for GUI ?
WD2793::DriveNum WD2793::getDriveSelect(const EmuTime &time)
{
	return current_drive;
}

void WD2793::setMotor(bool status, const EmuTime &time)
{
	if (current_drive != NO_DRIVE) {
		if (motorStatus[current_drive] != status) {
			motorStatus[current_drive] = status;
			motorStartTime[current_drive] = time;
		}
	}
}
//actually not used ,maybe for GUI ?
bool WD2793::getMotor(const EmuTime &time)
{
	if (current_drive != NO_DRIVE) {
		return motorStatus[current_drive];
	} else {
		return false;
	}
}

void WD2793::setSideSelect(bool side, const EmuTime &time)
{
	current_side = (int)side;
}

bool WD2793::getSideSelect(const EmuTime &time)
{
	return (bool)current_side;
}

bool WD2793::getDTRQ(const EmuTime &time)
{
	if (((commandReg & 0xF0) == 0xF0) &&
	    (statusReg & 1) &&
	    (current_drive != NO_DRIVE)) {
		// WRITE TRACK && status busy
		int ticks = DRQTime[current_drive].getTicksTill(time);
		if (ticks >= 15) { 
			// writing a byte during format takes +/- 33 us
			// according to tech data but on trubor fdc docs it
			// state 15 us to get data ready
			// TODO check for clockspeed of used diagram in wdc
			//      docs this could explain mistake
			DRQTime[current_drive] -= 15;
			DRQ = true;
		}
	}
	return DRQ;
}

bool WD2793::getIRQ(const EmuTime &time)
{
	return INTRQ;
	
	/* bool tmp = INTRQ;
	   INTRQ = false;
	   return tmp; */
}

void WD2793::setCommandReg(byte value, const EmuTime &time)
{
	commandReg = value;
	statusReg |= 1;	// set status on Busy
	INTRQ = false;
	DRQ = false;
	// commandEnd = commandStart = time;
	commandEnd = time;

	//First we set some flags from the lower four bits of the command
	Cflag	      = value & 0x02;
	stepSpeed     = value & 0x03;
	Eflag = Vflag = value & 0x04;
	hflag = Sflag = value & 0x08;
	mflag = Tflag = value & 0x10;
	// above code could by executed always with no if's
	// what-so-ever. Since the flags are always written during
	// a new command and only applicable 
	// for the current command
	// some confusion about Eflag/Vflag see below
	// flags for the Force Interrupt are ignored for now.

	switch (value & 0xF0) {
	case 0x00: //restore
		PRT_DEBUG("FDC command: restore");

		commandEnd += (current_track * timePerStep[stepSpeed]);
		if (Vflag) commandEnd += 15000; //Head setting time

		// according to page 1-100 last alinea, however not sure so ommited
		// if (Eflag) commandEnd += 15000;
		current_track = 0;
		trackReg = 0;
		directionIn = false;
		// TODO Ask ricardo about his "timeout" for a restore driveb ?
		statusReg &= ~1;	// reset status on Busy
		break;

	case 0x10: //seek
		PRT_DEBUG("FDC command: seek");
		//PRT_DEBUG("before: track "<<(int)trackReg<<",data "
		//          <<(int)dataReg<<",cur "<<(int)current_track);
		if (trackReg != dataReg) {
			// It could be that the current track isn't the one
			// indicated by the dataReg-sectorReg so we calculated
			// the steps the FDC will send to get the two regs the
			// same and add/distract this from the real current
			// track
			byte steps;
			if (trackReg < dataReg) {
				steps = dataReg - trackReg;
				current_track += steps;
				directionIn = true;
			} else {
				steps = trackReg - dataReg;
				current_track -= steps;
				directionIn = false;
			}
			trackReg = dataReg;

			commandEnd += (steps * timePerStep[stepSpeed]);
			if (Vflag) commandEnd += 15000;	//Head setting time

			//TODO actually verify
			//PRT_DEBUG("after : track "<<(int)trackReg<<",data "
			//        <<(int)dataReg<<",cur "<<(int)current_track);
			statusReg &= ~1;	// reset status on Busy
		}
		break;

	case 0x20: //step
	case 0x30: //step (Update trackRegister)
		PRT_DEBUG("FDC command: step (Tflag "<<(int)Tflag<<")");
		if (directionIn) {
			current_track++;
			if (Tflag) trackReg++;
		} else {
			current_track--;
			if (Tflag) trackReg--;
		}

		commandEnd += timePerStep[stepSpeed];
		if (Vflag) commandEnd += 15000; //Head setting time

		statusReg &= ~1;	// reset status on Busy
		break;

	case 0x40: //step-in
	case 0x50: //step-in (Update trackRegister)
		PRT_DEBUG("FDC command: step in (Tflag "<<(int)Tflag<<")");
		current_track++;
		directionIn = true;
		if (Tflag) trackReg++;

		commandEnd += timePerStep[stepSpeed];
		if (Vflag) commandEnd += 15000; //Head setting time

		statusReg &= ~1;	// reset status on Busy
		break;

	case 0x60: //step-out
	case 0x70: //step-out (Update trackRegister)
		PRT_DEBUG("FDC command: step out (Tflag "<<(int)Tflag<<")");
		// TODO Specs don't say what happens if track was already 0 !!
		current_track--;
		directionIn = false;
		if (Tflag) trackReg++;

		commandEnd += timePerStep[stepSpeed];
		if (Vflag) commandEnd += 15000;	//Head setting time

		statusReg &= ~1;	// reset status on Busy
		break;

	case 0x80: //read sector
	case 0x90: //read sector (multi)
		PRT_DEBUG("FDC command: read sector");
		//assert(!mflag);
		// no assert here !!!!
		// The mflag influences the way the command ends and is handled in the 
		// getDataReg method (were the assert should be if needed)
		INTRQ = false;
		DRQ = false;
		statusReg &= 0x01;	// reset lost data,record not found & status bits 5 & 6
		tryToReadSector();
		commandEnd += 1000000;	// TODO hack
		break;

	case 0xA0: // write sector
	case 0xB0: // write sector (multi)
		PRT_DEBUG("FDC command: write sector");
		INTRQ = false;
		statusReg &= 0x01;	// reset lost data,record not found & status bits 5 & 6
		dataCurrent = 0;
		dataAvailable = 512;	// TODO should come from sector header
		DRQ = true;	// data ready to be written
		commandEnd += 1000000;	// TODO hack
		break;

	case 0xC0: //Read Address
		PRT_DEBUG("FDC command: read address");
		PRT_INFO("FDC command not yet implemented");
		break;
		
	case 0xD0: //Force interrupt
		PRT_DEBUG("FDC command: Force interrupt statusregister "
		          <<(int)statusReg);
		statusReg &= ~1;	// reset status on Busy
		break;
		
	case 0xE0: //read track
		PRT_DEBUG("FDC command: read track");
		PRT_INFO("FDC command not yet implemented");
		break;
		
	case 0xF0: //write track
		PRT_DEBUG("FDC command: write track");
		statusReg &= 0x01;	// reset lost data,record not found & status bits 5 & 6
		DRQ = true;
		getBackEnd(current_drive)->
			initWriteTrack(current_track, trackReg,current_side); 
		PRT_DEBUG("WD2793::getBackEnd(current_drive)->initWriteTrack(current_track "
		          <<(int)current_track<<", trackReg "<<(int)trackReg
			  <<",current_side "<<(int)current_side<<")");

		//PRT_INFO("FDC command not yet implemented ");
		//CommandController::instance()->executeCommand(std::string("cpudebug"));
		//statusReg &= ~1;	// reset status on Busy
		// Variables below are a not-completely-correct hack:
		// Correct behavior would indicate that one waits until the
		// next indexmark before the first byte is written and that
		// from the command stays active until the next indexmark.
		//
		// By setting the motorStartTime to the current value we force
		// an imediate indexmark. Also the DTR line is "faked" now
		motorStartTime[current_drive] = time;
		DRQTime[current_drive] = time;
		break;
	}
}

void WD2793::tryToReadSector(void)
{
	dataCurrent = 0;
	dataAvailable = 512;	// TODO should come from sector header
	try {
		getBackEnd(current_drive)->
			read(current_track, trackReg, sectorReg,
			     current_side, 512, dataBuffer);
		// TODO  backend should make sure that combination of trackReg, sectorReg etc
		// is valid in case of multitrack read !!! and throw error !!! 
		DRQ = true;	// data ready to be read
	} catch (MSXException &e) {
		DRQ = false;	// TODO data not ready (read error)
		statusReg = 0;	// reset flags
	}
}

byte WD2793::getStatusReg(const EmuTime &time)
{
	if ((commandReg & 0x80) == 0) {
		// Type I command so bit 1 should be the index pulse
		if (current_drive != NO_DRIVE) {
			int ticks = motorStartTime[current_drive].getTicksTill(time);
			if (ticks >= 200000) {
				ticks -= 200000 * (int(ticks/200000));
			}
			// TODO: check on a real MSX how long this indexmark is
			// visible. According to Ricardo it is simply a sensor
			// that is mapped onto this bit that sees if the index
			// mark is passing by or not. (The little gap in the
			// metal plate of te disk)
			if (ticks < 20000) {
				statusReg |= 2;
			}
		}
	} else {
		// Not type I command so bit 1 should be DRQ
		if (getDTRQ(time)) {
			statusReg |= 2;
		} else {
			statusReg &= ~2;
		}
	}
	if (time >= commandEnd) {
		// reset BUSY bit
		statusReg &= ~1;
	}
	PRT_DEBUG("FDC: statusReg is "<<(int)statusReg);
	return statusReg;
}

void WD2793::setTrackReg(byte value,const EmuTime &time)
{
	trackReg = value;
}
byte WD2793::getTrackReg(const EmuTime &time)
{
	return trackReg;
}

void WD2793::setSectorReg(byte value,const EmuTime &time)
{
	sectorReg = value;
}
byte WD2793::getSectorReg(const EmuTime &time)
{
	return sectorReg;
}

void WD2793::setDataReg(byte value, const EmuTime &time)
{
	// TODO Is this also true in case of sector write?
	//      Not so according to ASM of brMSX
	dataReg = value;
	if ((commandReg & 0xE0) == 0xA0) {
		// WRITE SECTOR
		dataBuffer[dataCurrent] = value;
		dataCurrent++;
		dataAvailable--;
		if (dataAvailable == 0) {
			PRT_DEBUG("FDC: Now we call the backend to write a sector");
			try {
				getBackEnd(current_drive)->
					write(current_track, trackReg,
					      sectorReg, current_side, 512,
					      dataBuffer);
				// If we wait too long we should also write a
				// partialy filled sector ofcourse and set the
				// correct status bits!
				statusReg &= ~0x03;	// reset status on Busy and DRQ
				if (mflag == 0) {
					//TODO verify this !
					INTRQ = true;
					DRQ = false;
				}
				dataCurrent = 0;
				dataAvailable = 512; // TODO should come from sector header
			} catch (MSXException &e) {
				// Backend couldn't write data
			}
		}
	} else if ((commandReg & 0xF0) == 0xF0) {
		// WRITE TRACK
		if (current_side != NO_DRIVE) {
			//PRT_DEBUG("WD2793 WRITE TRACK value "<<std::hex<<
			//          (int)value<<std::dec);
			int ticks = motorStartTime[current_drive].getTicksTill(time);
			//PRT_DEBUG("WD2793 WRITE TRACK ticks "<<(int)ticks);
			//DRQ related timing
			DRQ = false;
			DRQTime[current_drive] = time;
			//indexmark related timing
			if (ticks >= 200000) {
				//next indexmark has passed
				dataAvailable = 0; //return correct DTR
				statusReg &= ~0x03;	// reset status on Busy and DRQ
				DRQ = false;
				INTRQ = true;
				dataCurrent = 0;
			} else {
				getBackEnd(current_drive)->writeTrackData(value); 
			}
			/* followin switch stement belongs in the backend, since
			 * we do not know how the actual diskimage stores the
			 * data. It might simply drop all the extra CRC/header
			 * stuff and just use some of the switches to actually
			 * simply write a 512 bytes sector.
			 *
			 * However, timing should be done here :-\
			 *

			 switch (value) {
			 case 0xFE:
			 case 0xFD:
			 case 0xFC:
			 case 0xFB:
			 case 0xFA:
			 case 0xF9:
			 case 0xF8:
				PRT_DEBUG("CRC generator initializing");
				break;
			 case 0xF6:
				PRT_DEBUG("write C2 ?");
				break;
			 case 0xF5:
				PRT_DEBUG("CRC generator initializing in MFM, write A1?");
				break;
			 case 0xF7:
				PRT_DEBUG("two CRC characters");
				break;
			 default:
				//Normal write to track
				break;
			 }
			 //shouldn't been done here !!
			 statusReg &= ~0x03;	// reset status on Busy and DRQ
			 */


			/*
			   if (indexmark) {
			   statusReg &= ~0x03;	// reset status on Busy and DRQ
			   INTRQ = true;
			   DRQ = false; 
			   }
			 */
		}
	}
}

byte WD2793::getDataReg(const EmuTime &time)
{
	if ((commandReg & 0xE0) == 0x80) {
		// READ SECTOR
		dataReg = dataBuffer[dataCurrent];
		dataCurrent++;
		dataAvailable--;
		if (dataAvailable == 0) {
			if (!mflag){
				statusReg &= ~0x03;	// reset status on Busy and DRQ
				DRQ = false;
				INTRQ = true;
				PRT_DEBUG("FDC: Now we terminate the read sector command or skip to next sector if multi set");
			} else {
				// TODO ceck in tech data (or on real machine)
				// if implementation multi sector read is
				// correct, since this is programmed by hart.
				sectorReg++;
				tryToReadSector();
			}
		}
	}
	return dataReg;
}
