#include <iostream>
#include <string>
#include <chrono>
#include <thread>

#include <stdlib.h>

#include "util.hpp"

/*** Function Implementation ***/

void throw_error(std::string msg, int rc)
{
	std::cerr << msg << std::endl;
	exit(rc);
}

void throw_perror(std::string msg, int rc)
{
	perror(msg.c_str());
	exit(rc);
}

void sleep_for_ms(int ms)
{
	if (ms > 0)
		std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

/*** Timer Class ***/

Timer::Timer()
{
	tic();
}

Timer::~Timer() { }

void
Timer::tic()
{
	t0 = get_current_time();
	stopped = false;
}

unsigned long
Timer::toc()
{
	if (stopped)
		return 0;
	else
		return get_current_time() - t0;
}

void
Timer::stop()
{
	stopped = true;
}

unsigned long
Timer::get_current_time()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}
