// $Id$

#ifndef SCHEDULER_HH
#define SCHEDULER_HH

#include "EmuTime.hh"
#include "Semaphore.hh"
#include "Schedulable.hh"
#include "likely.hh"
#include "noncopyable.hh"
#include <vector>

namespace openmsx {

class PollInterface;

class Scheduler : private noncopyable
{
private:
	class SynchronizationPoint
	{
	public:
		SynchronizationPoint(const EmuTime& time,
				     Schedulable* dev, int usrdat)
			: timeStamp(time), device(dev), userData(usrdat) {}
		const EmuTime& getTime() const { return timeStamp; }
		Schedulable* getDevice() const { return device; }
		int getUserData() const { return userData; }
	private:
		EmuTime timeStamp;
		Schedulable* device;
		int userData;
	};
	struct LessSyncPoint {
		bool operator()(
			const EmuTime& time,
			const Scheduler::SynchronizationPoint& sp) const
		{
			return time < sp.getTime();
		}
		bool operator()(
			const Scheduler::SynchronizationPoint& sp,
			const EmuTime& time) const
		{
			return sp.getTime() < time;
		}
	};

	struct FindSchedulable {
		explicit FindSchedulable(Schedulable& schedulable_)
			: schedulable(schedulable_) {}
		bool operator()(Scheduler::SynchronizationPoint& sp) const {
			return sp.getDevice() == &schedulable;
		}
		Schedulable& schedulable;
	};

public:
	Scheduler();
	~Scheduler();

	/**
	 * Get the current scheduler time.
	 */
	const EmuTime& getCurrentTime() const;

	/**
	 * TODO
	 */
	inline const EmuTime& getNext() const
	{
		const EmuTime& time = syncPoints.front().getTime();
		return time == ASAP ? scheduleTime : time;
	}

	/**
	 * Schedule till a certain moment in time.
	 */
	inline void schedule(const EmuTime& limit)
	{
		// TODO: Assumes syncPoints is not empty.
		//       In practice that's true because VDP end-of-frame sync point
		//       is always there, but it's ugly to rely on that.
		if (unlikely(limit >= syncPoints.front().getTime())) {
			scheduleHelper(limit); // slow path not inlined
		}
		scheduleTime = limit;
	}

	// TODO move to reactor?
	void   registerPoll(PollInterface& poll);
	void unregisterPoll(PollInterface& poll);
	void doPoll();

	static const EmuTime ASAP;

private: // -> intended for Schedulable
	friend void Schedulable::setSyncPoint(const EmuTime&, int);
	friend void Schedulable::removeSyncPoint(int);
	friend void Schedulable::removeSyncPoints();
	friend bool Schedulable::pendingSyncPoint(int);

	/**
	 * Register a syncPoint. When the emulation reaches "timestamp",
	 * the executeUntil() method of "device" gets called.
	 * SyncPoints are ordered: smaller EmuTime -> scheduled
	 * earlier.
	 * The supplied EmuTime may not be smaller than the current CPU
	 * time.
	 * If you want to schedule something as soon as possible, you
	 * can pass Scheduler::ASAP as time argument.
	 * A device may register several syncPoints.
	 * Optionally a "userData" parameter can be passed, this
	 * parameter is not used by the Scheduler but it is passed to
	 * the executeUntil() method of "device". This is useful
	 * if you want to distinguish between several syncPoint types.
	 * If you do not supply "userData" it is assumed to be zero.
	 */
	void setSyncPoint(const EmuTime& timestamp, Schedulable& device,
	                  int userData = 0);

	/**
	 * Removes a syncPoint of a given device that matches the given
	 * userData.
	 * If there is more than one match only one will be removed,
	 * there is no guarantee that the earliest syncPoint is
	 * removed.
	 */
	void removeSyncPoint(Schedulable& device, int userdata = 0);

	/** Remove all syncpoints for the given device.
	  */
	void removeSyncPoints(Schedulable& device);

	/**
	 * Is there a pending syncPoint for this device?
	 */
	bool pendingSyncPoint(Schedulable& device, int userdata = 0);

private:
	void scheduleHelper(const EmuTime& limit);

	/** Vector used as heap, not a priority queue because that
	  * doesn't allow removal of non-top element.
	  */
	typedef std::vector<SynchronizationPoint> SyncPoints;
	SyncPoints syncPoints;
	Semaphore sem;	// protects syncPoints

	EmuTime scheduleTime;

	typedef std::vector<PollInterface*> PollInterfaces;
	PollInterfaces pollInterfaces;
};

} // namespace openmsx

#endif
