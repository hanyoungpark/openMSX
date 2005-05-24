// $Id$

#ifndef LEDEVENT_HH
#define LEDEVENT_HH

#include "Event.hh"

namespace openmsx {

class LedEvent : public Event
{
public:
	enum Led {
		POWER,
		CAPS,
		KANA, // same as CODE LED
		PAUSE,
		TURBO,
		FDD,
		NUM_LEDS // must be last
	};

	LedEvent(Led led_, bool status_)
		: Event(OPENMSX_LED_EVENT)
		, led(led_)
		, status(status_) {}

	Led getLed() const { return led; }
	bool getStatus() const { return status; }

private:
	Led led;
	bool status;
};

} // namespace openmsx

#endif
