#pragma once

#include "string.h"

#include <atomic>

class Logger {
private:
	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;

	FILE* f_;
	std::atomic<bool> opened_{false}, label_{true};

	void close() noexcept {
		if (opened_.load()) {
			opened_.store(false);
			fclose(f_);
		}
	}

	// Lock the file
	bool lock() noexcept {
		if (f_ == nullptr)
			return false;

		flockfile(f_);
		return true;
	}

	// Unlock the file
	void unlock() noexcept { funlockfile(f_); }

public:
	// Like open()
	explicit Logger(const CStringView& filename);

	// Like use()
	explicit Logger(FILE *stream) noexcept : f_(stream) {}

	/**
	 * @brief Opens file @p filename in append mode as log file, if fopen()
	 *   error occurs exception is thrown and f_ (inner stream) is unchanged
	 *
	 * @param filename file to open
	 *
	 * @errors Throws an exception std::runtime_error if an fopen() error occurs
	 */
	void open(const CStringView& filename);

	/// Use @p stream as log stream
	void use(FILE *stream) noexcept {
		close();
		f_ = stream;
	}

	/// Returns file descriptor which is used internally by Logger (to log to
	/// it)
	int fileno() const noexcept { return ::fileno(f_); }

	bool label() const noexcept {
		return label_.load(std::memory_order_relaxed);
	}

	bool label(bool add_label) noexcept { return label_.exchange(add_label); }

	class Appender {
	private:
		friend class Logger;

		Logger& logger_;
		bool flushed_ = true;
		std::string buff_;

		explicit Appender(Logger& logger) : logger_(logger) {}

		template<class... Args>
		explicit Appender(Logger& logger, Args&&... args) : logger_(logger) {
			operator()(std::forward<Args>(args)...);
		}

	public:
		Appender(const Appender&) = delete;

		Appender(Appender&& app) : logger_(app.logger_), flushed_(app.flushed_),
			buff_(std::move(app.buff_))
		{
			app.flushed_ = true;
		}

		Appender& operator=(const Appender&) = delete;

		Appender& operator=(Appender&&) = delete;

		template<class T>
		Appender& operator<<(T&& x) {
			return operator()(std::forward<T>(x));
		}

		template<class... Args>
		Appender& operator()(Args&&... args) {
			size_t total_length = buff_.size();
			int foo[] = {(total_length += string_length(args), 0)...};
			(void)foo;

			buff_.reserve(total_length);
			int bar[] = {(buff_ += std::forward<Args>(args), 0)...};
			(void)bar;

			flushed_ = false;
			return *this;
		}

		void flush() noexcept;

		~Appender() { flush(); }
	};

	template<class T>
	Appender operator<<(T&& x) noexcept {
		return Appender(*this, std::forward<T>(x));
	}

	template<class... Args>
	Appender operator()(Args&&... args) noexcept {
		return Appender(*this, std::forward<Args>(args)...);
	}

	Appender getAppender() noexcept { return Appender(*this); }

	~Logger() {
		if (opened_)
			fclose(f_);
	}
};

// By default both write to stderr
extern Logger stdlog; // Standard (default) log
extern Logger errlog; // Error log
