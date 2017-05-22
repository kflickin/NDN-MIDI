#ifndef UTIL_HPP_
#define UTIL_HPP_

#include <string>

#define bool2int(val) ((val) ? 1 : 0)
#define int2bool(val) ((val) != 0)

// print message to stderr and exit (optional exit code)
void throw_error(std::string msg, int rc = 1);

// call perror and ext (optional exit code)
void throw_perror(std::string msg, int rc = 1);

// syntactic sugar for the long sleep statement using chrono
void sleep_for_ms(int ms);

class Timer
{
public:
	Timer();
	~Timer();

	bool stopped;

	// starts timer
	void tic();
	// returns time after start in ms; if timer is stopped, returns 0
	unsigned long toc();
	// stops timer; use tic() to start again
	void stop();

private:
	unsigned long t0;
	unsigned long get_current_time();	// unit is millisecond
};

#endif
