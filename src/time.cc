#include "../include/debug.h"

#include <algorithm>
#include <ctime>
#include <string>
#include <sys/time.h>

using std::string;

long long microtime() noexcept {
	timeval mtime;
	(void)gettimeofday(&mtime, nullptr);
	return (mtime.tv_sec * 1000000LL) + mtime.tv_usec;
}

template<class F>
static string __date(const CStringView& format, time_t curr_time, F func) {
	if (curr_time < 0)
		time(&curr_time);

	string buff(format.size() + 1 +
		std::count(format.begin(), format.end(), '%') * 25, '0');

	tm *ptm = func(&curr_time);
	if (!ptm)
		THROW("Failed to convert time");

	size_t rc = strftime(const_cast<char*>(buff.data()), buff.size(),
		format.c_str(), ptm);

	buff.resize(rc);
	return buff;
}

string date(const CStringView& format, time_t curr_time) {
	return __date(format, curr_time, gmtime);
}

string localdate(const CStringView& format, time_t curr_time) {
	return __date(format, curr_time, localtime);
}

bool isDatetime(const CStringView& str) noexcept {
	struct tm t;
	return (str.size() == 19 &&
			strptime(str.c_str(), "%Y-%m-%d %H:%M:%S", &t) != nullptr);
}

time_t strToTime(const CStringView& str, const CStringView& format) noexcept {
	struct tm t;
	if (!strptime(str.c_str(), format.c_str(), &t))
		return -1;

	return timegm(&t);
}
