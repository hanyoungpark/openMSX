// $Id$

#include <cassert>
#include <SDL.h>	// TODO move this
#include "Joystick.hh"
#include "PluggingController.hh"
#include "EventDistributor.hh"
#include "InputEvents.hh"

using std::string;

namespace openmsx {

void Joystick::registerAll(PluggingController *controller)
{
	if (!SDL_WasInit(SDL_INIT_JOYSTICK)) {
		SDL_InitSubSystem(SDL_INIT_JOYSTICK);
		SDL_JoystickEventState(SDL_ENABLE);	// joysticks generate events
	}

	unsigned numJoysticks = SDL_NumJoysticks();
	for (unsigned i = 0; i < numJoysticks; i++) {
		controller->registerPluggable(new Joystick(i));
	}
}

Joystick::Joystick(unsigned joyNum_)
	: joyNum(joyNum_)
{
	PRT_DEBUG("Creating a Joystick object for joystick " << joyNum);
	assert(joyNum < (unsigned)SDL_NumJoysticks());
	name = string("joystick") + (char)('1' + joyNum);
	desc = string(SDL_JoystickName(joyNum));
}

Joystick::~Joystick()
{
}

//Pluggable
const string& Joystick::getName() const
{
	return name;
}

const string& Joystick::getDescription() const
{
	return desc;
}

void Joystick::plugHelper(Connector* /*connector*/, const EmuTime& /*time*/)
{
	PRT_DEBUG("Opening joystick " << SDL_JoystickName(joyNum));
	joystick = SDL_JoystickOpen(joyNum);
	if (!joystick) {
		throw PlugException("Failed to open joystick device");
	}

	EventDistributor::instance().registerEventListener(
		OPENMSX_JOY_AXIS_MOTION_EVENT, *this);
	EventDistributor::instance().registerEventListener(
		OPENMSX_JOY_BUTTON_DOWN_EVENT, *this);
	EventDistributor::instance().registerEventListener(
		OPENMSX_JOY_BUTTON_UP_EVENT,   *this);

	status = JOY_UP | JOY_DOWN | JOY_LEFT | JOY_RIGHT |
	         JOY_BUTTONA | JOY_BUTTONB;
}

void Joystick::unplugHelper(const EmuTime& /*time*/)
{
	EventDistributor::instance().unregisterEventListener(
		OPENMSX_JOY_AXIS_MOTION_EVENT, *this);
	EventDistributor::instance().unregisterEventListener(
		OPENMSX_JOY_BUTTON_DOWN_EVENT, *this);
	EventDistributor::instance().unregisterEventListener(
		OPENMSX_JOY_BUTTON_UP_EVENT,   *this);

	SDL_JoystickClose(joystick);
}


//JoystickDevice
byte Joystick::read(const EmuTime& /*time*/)
{
	return status;
}

void Joystick::write(byte /*value*/, const EmuTime& /*time*/)
{
	//do nothing
}

//EventListener
bool Joystick::signalEvent(const Event& event)
{
	assert(dynamic_cast<const JoystickEvent*>(&event));
	const JoystickEvent& joyEvent = static_cast<const JoystickEvent&>(event);
	if (joyEvent.getJoystick() != joyNum) {
		return true;
	}

	switch (event.getType()) {
	case OPENMSX_JOY_AXIS_MOTION_EVENT: {
		assert(dynamic_cast<const JoystickAxisMotionEvent*>(&event));
		const JoystickAxisMotionEvent& motionEvent =
			static_cast<const JoystickAxisMotionEvent&>(event);
		short value = motionEvent.getValue();
		switch (motionEvent.getAxis()) {
		case JoystickAxisMotionEvent::X_AXIS: // Horizontal
			if (value < -THRESHOLD) {
				status &= ~JOY_LEFT;	// left      pressed
				status |=  JOY_RIGHT;	// right not pressed
			} else if (value > THRESHOLD) {
				status |=  JOY_LEFT;	// left  not pressed
				status &= ~JOY_RIGHT;	// right     pressed
			} else {
				status |=  JOY_LEFT;;	// left  not pressed
				status |=  JOY_RIGHT;	// right not pressed
			}
			break;
		case JoystickAxisMotionEvent::Y_AXIS: // Vertical
			if (value < -THRESHOLD) {
				status |=  JOY_DOWN;	// down not pressed
				status &= ~JOY_UP;	// up       pressed
			} else if (value > THRESHOLD) {
				status &= ~JOY_DOWN;	// down     pressed
				status |=  JOY_UP;	// up   not pressed
			} else {
				status |=  JOY_DOWN;	// down not pressed
				status |=  JOY_UP;	// up   not pressed
			}
			break;
		default:
			// ignore other axis
			break;
		}
		break;
	}
	case OPENMSX_JOY_BUTTON_DOWN_EVENT: {
		assert(dynamic_cast<const JoystickButtonEvent*>(&event));
		const JoystickButtonEvent& buttonEvent =
			static_cast<const JoystickButtonEvent&>(event);
		switch (buttonEvent.getButton()) {
		case 0:
			status &= ~JOY_BUTTONA;
			break;
		case 1:
			status &= ~JOY_BUTTONB;
			break;
		default:
			// ignore other buttons
			break;
		}
		break;
	}
	case OPENMSX_JOY_BUTTON_UP_EVENT: {
		assert(dynamic_cast<const JoystickButtonEvent*>(&event));
		const JoystickButtonEvent& buttonEvent =
			static_cast<const JoystickButtonEvent&>(event);
		switch (buttonEvent.getButton()) {
		case 0:
			status |= JOY_BUTTONA;
			break;
		case 1:
			status |= JOY_BUTTONB;
			break;
		default:
			// ignore other buttons
			break;
		}
		break;
	}
	default:
		assert(false);
	}
	return true;
}

} // namespace openmsx
