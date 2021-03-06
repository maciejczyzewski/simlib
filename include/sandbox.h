#pragma once

#include "meta.h"
#include "process.h"
#include "spawner.h"
#include "syscall_name.h"
#include "utilities.h"

#include <cstddef>
#include <sys/ptrace.h>

class Sandbox : protected Spawner {
public:
	Sandbox() = delete;

	struct i386_user_regset;

	struct x86_64_user_regset;

private:
	struct Registers;

public:
	class CallbackBase {
	protected:
		int8_t arch = -1; // arch - architecture: 0 - i386, 1 - x86_64

		/**
		 * @brief Checks whether syscall open(2) is allowed, if not, tries to
		 *   modify it (by replacing filename with nullptr) so that syscall will
		 *   fail
		 *
		 * @param pid pid of traced process (via ptrace)
		 * @param allowed_filed files which are allowed to be opened
		 *
		 * @return true if call is allowed (modified if needed), false otherwise
		 */
		bool isSysOpenAllowed(pid_t pid,
			const std::vector<std::string>& allowed_files = {});

		/**
		 * @brief Checks whether syscall lseek(2) is allowed, if not, tries to
		 *   modify it (by replacing fd with -1) so that syscall will fail
		 *
		 * @param pid pid of traced process (via ptrace)
		 *
		 * @return true if call is allowed (modified if needed), false otherwise
		 */
		bool isSysLseekAllowed(pid_t pid);

		/**
		 * @brief Checks whether syscall tgkill(2) is allowed
		 *
		 * @details Syscall is allowed only if the first and the second argument
		 *   are equal to @p pid
		 *
		 * @param pid pid of traced process (via ptrace)
		 *
		 * @return true if call is allowed, false otherwise
		 */
		bool isSysTgkillAllowed(pid_t pid);

	public:
		void detectTraceeArchitecture(pid_t pid) {
			arch = ::detectArchitecture(pid);
		}

		int8_t getArch() const noexcept { return arch; }

		/**
		 * @brief Checks whether or not entering syscall @p syscall is allowed
		 *
		 * @param pid sandboxed process id
		 * @param syscall executed syscall number to check
		 *
		 * @return true if syscall is allowed to be executed, false otherwise
		 */
		virtual bool isSyscallEntryAllowed(pid_t pid, int syscall) = 0;

		/**
		 * @brief Checks whether or not exit from finished syscall @p syscall
		 *   is allowed
		 *
		 * @param pid sandboxed process id
		 * @param syscall executed syscall number to check
		 *
		 * @return true if finished syscall is allowed to exit, false otherwise
		 */
		virtual bool isSyscallExitAllowed(pid_t pid, int syscall) = 0;

		/**
		 * @brief Returns error message which was set after unsuccessful call to
		 *   either isSyscallEntryAllowed() or isSyscallExitAllowed()
		 * @return an error message
		 */
		virtual std::string errorMessage() const = 0;

		virtual ~CallbackBase() noexcept {}
	};

	class DefaultCallback : public CallbackBase {
	protected:
		struct Pair {
			int syscall;
			int limit;
		};

		static_assert(ARCH_i386 == 0, "Invalid value of a constant");
		static_assert(ARCH_x86_64 == 1, "Invalid value of a constant");

		std::vector<Pair> limited_syscalls[2] = {
			{ /* i386 */
				{  11, 1 }, // SYS_execve
				{  33, 1 }, // SYS_access
				{  85, 1 }, // SYS_readlink
				{ 122, 1 }, // SYS_uname
				{ 243, 1 }, // SYS_set_thread_area
			},
			{ /* x86_64 */
				{  21, 1 }, // SYS_access
				{  59, 1 }, // SYS_execve
				{  63, 1 }, // SYS_uname
				{  89, 1 }, // SYS_readlink
				{ 158, 1 }, // SYS_arch_prctl
				{ 205, 1 }, // SYS_set_thread_area
			}
		};

		int unsuccessful_SYS_brk_counter = 0; // used in isSyscallExitAllowed()
		static constexpr int UNSUCCESSFUL_SYS_BRK_LIMIT = 128;

		std::string error_message;

		/// Check whether syscall @p syscall lies in proper syscall_list
		template<size_t N1, size_t N2>
		bool isSyscallIn(int syscall,
			const std::array<int, N1>& syscall_list_i386,
			const std::array<int, N2>& syscall_list_x86_64);

		/// Check whether syscall @p syscall is allowed
		template<size_t N1, size_t N2>
		bool isSyscallEntryAllowed(pid_t pid, int syscall,
			const std::array<int, N1>& allowed_syscalls_i386,
			const std::array<int, N2>& allowed_syscalls_x86_64,
			const std::vector<std::string>& allowed_files);

		bool isSyscallEntryAllowed(pid_t pid, int syscall,
			const std::vector<std::string>& allowed_files)
		{
			// TODO: make the below syscall numbers more portable
			constexpr std::array<int, 78> allowed_syscalls_i386 {{
				1, // SYS_exit
				3, // SYS_read
				4, // SYS_write
				6, // SYS_close
				13, // SYS_time
				20, // SYS_getpid
				24, // SYS_getuid
				27, // SYS_alarm
				29, // SYS_pause
				45, // SYS_brk
				47, // SYS_getgid
				49, // SYS_geteuid
				50, // SYS_getegid
				67, // SYS_sigaction
				72, // SYS_sigsuspend
				73, // SYS_sigpending
				76, // SYS_getrlimit
				77, // SYS_getrusage
				78, // SYS_gettimeofday
				82, // SYS_select
				90, // SYS_mmap
				91, // SYS_munmap
				100, // SYS_fstatfs
				108, // SYS_fstat
				118, // SYS_fsync
				125, // SYS_mprotect
				142, // SYS__newselect
				143, // SYS_flock
				144, // SYS_msync
				145, // SYS_readv
				146, // SYS_writev
				148, // SYS_fdatasync
				150, // SYS_mlock
				151, // SYS_munlock
				152, // SYS_mlockall
				153, // SYS_munlockall
				162, // SYS_nanosleep
				163, // SYS_mremap
				168, // SYS_poll
				174, // SYS_rt_sigaction
				175, // SYS_rt_sigprocmask
				176, // SYS_rt_sigpending
				177, // SYS_rt_sigtimedwait
				179, // SYS_rt_sigsuspend
				180, // SYS_pread64
				181, // SYS_pwrite64
				184, // SYS_capget
				187, // SYS_sendfile
				191, // SYS_ugetrlimit
				192, // SYS_mmap2
				197, // SYS_fstat64
				199, // SYS_getuid32
				200, // SYS_getgid32
				201, // SYS_geteuid32
				202, // SYS_getegid32
				219, // SYS_madvise
				224, // SYS_gettid
				231, // SYS_fgetxattr
				232, // SYS_listxattr
				239, // SYS_sendfile64
				240, // SYS_futex
				244, // SYS_get_thread_area
				250, // SYS_fadvise64
				252, // SYS_exit_group
				265, // SYS_clock_gettime
				266, // SYS_clock_getres
				267, // SYS_clock_nanosleep
				269, // SYS_fstatfs64
				272, // SYS_fadvise64_64
				308, // SYS_pselect6
				309, // SYS_ppoll
				312, // SYS_get_robust_list
				323, // SYS_eventfd
				328, // SYS_eventfd2
				333, // SYS_preadv
				334, // SYS_pwritev
				355, // SYS_getrandom
				376, // SYS_mlock2
			}};
			constexpr std::array<int, 64> allowed_syscalls_x86_64 {{
				0, // SYS_read
				1, // SYS_write
				3, // SYS_close
				5, // SYS_fstat
				7, // SYS_poll
				9, // SYS_mmap
				10, // SYS_mprotect
				11, // SYS_munmap
				12, // SYS_brk
				13, // SYS_rt_sigaction
				14, // SYS_rt_sigprocmask
				17, // SYS_pread64
				18, // SYS_pwrite64
				19, // SYS_readv
				20, // SYS_writev
				23, // SYS_select
				25, // SYS_mremap
				26, // SYS_msync
				28, // SYS_madvise
				34, // SYS_pause
				35, // SYS_nanosleep
				37, // SYS_alarm
				39, // SYS_getpid
				40, // SYS_sendfile
				60, // SYS_exit
				73, // SYS_flock
				74, // SYS_fsync
				75, // SYS_fdatasync
				96, // SYS_gettimeofday
				97, // SYS_getrlimit
				98, // SYS_getrusage
				102, // SYS_getuid
				104, // SYS_getgid
				107, // SYS_geteuid
				108, // SYS_getegid
				125, // SYS_capget
				127, // SYS_rt_sigpending
				128, // SYS_rt_sigtimedwait
				130, // SYS_rt_sigsuspend
				138, // SYS_fstatfs
				149, // SYS_mlock
				150, // SYS_munlock
				151, // SYS_mlockall
				152, // SYS_munlockall
				186, // SYS_gettid
				193, // SYS_fgetxattr
				196, // SYS_flistxattr
				201, // SYS_time
				202, // SYS_futex
				211, // SYS_get_thread_area
				221, // SYS_fadvise64
				228, // SYS_clock_gettime
				229, // SYS_clock_getres
				230, // SYS_clock_nanosleep
				231, // SYS_exit_group
				270, // SYS_pselect6
				271, // SYS_ppoll
				274, // SYS_get_robust_list
				284, // SYS_eventfd
				290, // SYS_eventfd2
				295, // SYS_preadv
				296, // SYS_pwritev
				318, // SYS_getrandom
				325, // SYS_mlock2t
			}};

			static_assert(meta::is_sorted(allowed_syscalls_i386), "");
			static_assert(meta::is_sorted(allowed_syscalls_x86_64), "");

			return isSyscallEntryAllowed(pid, syscall, allowed_syscalls_i386,
				allowed_syscalls_x86_64, allowed_files);
		}

	public:
		DefaultCallback() = default;

		virtual ~DefaultCallback() = default;

		bool isSyscallEntryAllowed(pid_t pid, int syscall) {
			return isSyscallEntryAllowed(pid, syscall, {});
		}

		bool isSyscallExitAllowed(pid_t pid, int syscall);

		std::string errorMessage() const { return error_message; }
	};

	using Spawner::ExitStat;
	using Spawner::Options;

	/**
	 * @brief Runs @p exec with arguments @p args with limits @p opts.time_limit
	 *   and @p opts.memory_limit under ptrace(2)
	 * @details Callback object is called on every syscall entry called by exec
	 *   with parameters: child pid, syscall number.
	 *   Callback::operator() must return whether syscall is allowed or not
	 *   @p exec is called via execvp()
	 *   This function is thread-safe
	 *
	 * @param exec filename that is to be executed
	 * @param args arguments passed to exec
	 * @param opts options (new_stdin_fd, new_stdout_fd, new_stderr_fd - file
	 *   descriptors to which respectively stdin, stdout, stderr of sandboxed
	 *   process will be changed or if negative, closed;
	 *   time_limit set to 0 disables time limit;
	 *   memory_limit set to 0 disables memory limit)
	 * @param working_dir directory at which @p exec will be run
	 * @param func callback functor (used to determine if syscall should be
	 *   executed)
	 *
	 * @return Returns ExitStat structure with fields:
	 *   - code: return status (in the format specified in wait(2)).
	 *   - runtime: in microseconds [usec]
	 *   - vm_peak: peak virtual memory size [bytes]
	 *   - message: detailed info about error, disallowed syscall, etc.
	 *
	 * @errors Throws an exception std::runtime_error with appropriate
	 *   information if any syscall fails
	 */
	template<class Callback = DefaultCallback>
	static ExitStat run(const CStringView& exec,
		const std::vector<std::string>& args, const Options& opts = Options(),
		const CStringView& working_dir = CStringView {"."},
		Callback&& func = Callback())
	{
		static_assert(std::is_base_of<CallbackBase, Callback>::value,
			"Callback has to derive from Sandbox::CallbackBase");
		return Spawner::runWithTimer<Impl, Callback>(opts.time_limit, exec,
			args, opts, working_dir, std::forward<Callback>(func));
	}

private:
	template<class Callback, class Timer>
	struct Impl {
		/**
		 * @brief Executes @p exec with arguments @p args with limits @p
		 *   opts.time_limit and @p opts.memory_limit under ptrace(2)
		 * @details Callback object is called on every syscall entry called by
		 *   exec with parameters: child pid, syscall number.
		 *   Callback::operator() must return whether syscall is allowed or not
		 *   @p exec is called via execvp()
		 *   This function is not thread-safe
		 *
		 * @param exec filename that is to be executed
		 * @param args arguments passed to exec
		 * @param opts options (new_stdin_fd, new_stdout_fd, new_stderr_fd -
		 *   - file descriptors to which respectively stdin, stdout, stderr of
		 *   sandboxed process will be changed or if negative, closed;
		 *   time_limit set to 0 disables time limit;
		 *   memory_limit set to 0 disables memory limit)
		 * @param working_dir directory at which @p exec will be run
		 * @param func callback functor (used to determine if syscall should be
		 *   executed)
		 *
		 * @return Returns ExitStat structure with fields:
		 *   - code: return status (in the format specified in wait(2)).
		 *   - runtime: in microseconds [usec]
		 *   - vm_peak: peak virtual memory size [bytes]
		 *   - message: detailed info about error, disallowed syscall, etc.
		 *
		 * @errors Throws an exception std::runtime_error with appropriate
		 *   information if any syscall fails
		 */
		static ExitStat execute(const CStringView& exec,
			const std::vector<std::string>& args, const Options& opts,
			const CStringView& working_dir, Callback func);
	};
};

/******************************* IMPLEMENTATION *******************************/

#if 0
# warning "Before committing disable this debug"
# define DEBUG_SANDBOX(...) __VA_ARGS__
#else
# define DEBUG_SANDBOX(...)
#endif

struct Sandbox::i386_user_regset {
	uint32_t ebx;
	uint32_t ecx;
	uint32_t edx;
	uint32_t esi;
	uint32_t edi;
	uint32_t ebp;
	uint32_t eax;
	uint32_t xds;
	uint32_t xes;
	uint32_t xfs;
	uint32_t xgs;
	uint32_t orig_eax;
	uint32_t eip;
	uint32_t xcs;
	uint32_t eflags;
	uint32_t esp;
	uint32_t xss;
};

struct Sandbox::x86_64_user_regset {
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t rbp;
	uint64_t rbx;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rax;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t orig_rax;
	uint64_t rip;
	uint64_t cs;
	uint64_t eflags;
	uint64_t rsp;
	uint64_t ss;
	uint64_t fs_base;
	uint64_t gs_base;
	uint64_t ds;
	uint64_t es;
	uint64_t fs;
	uint64_t gs;
};

struct Sandbox::Registers {
	union user_regs_union {
		i386_user_regset i386_regs;
		x86_64_user_regset x86_64_regs;
	} uregs;

	Registers() = default;

	void getRegs(pid_t pid) {
		struct iovec ivo = {
			&uregs,
			sizeof(uregs)
		};
		if (ptrace(PTRACE_GETREGSET, pid, 1, &ivo) == -1)
			THROW("Error: ptrace(PTRACE_GETREGS)", error(errno));
	}

	void setRegs(pid_t pid) {
		struct iovec ivo = {
			&uregs,
			sizeof(uregs)
		};
		// Update traced process registers
		if (ptrace(PTRACE_SETREGSET, pid, 1, &ivo) == -1)
			THROW("Error: ptrace(PTRACE_SETREGS)", error(errno));
	}
};

template<size_t N1, size_t N2>
bool Sandbox::DefaultCallback::isSyscallIn(int syscall,
	const std::array<int, N1>& syscall_list_i386,
	const std::array<int, N2>& syscall_list_x86_64)
{
	if (arch == ARCH_i386)
		return binary_search(syscall_list_i386, syscall);

	return binary_search(syscall_list_x86_64, syscall);
}

template<size_t N1, size_t N2>
bool Sandbox::DefaultCallback::isSyscallEntryAllowed(pid_t pid, int syscall,
	const std::array<int, N1>& allowed_syscalls_i386,
	const std::array<int, N2>& allowed_syscalls_x86_64,
	const std::vector<std::string>& allowed_files)
{
	// Check if syscall is allowed
	if (isSyscallIn(syscall, allowed_syscalls_i386, allowed_syscalls_x86_64))
		return true;

	// Check if syscall is limited
	for (Pair& i : limited_syscalls[arch])
		if (syscall == i.syscall)
			return (--i.limit >= 0);

	constexpr int sys_open[2] = {
		5, // SYS_open - i386
		2 // SYS_open - x86_64
	};
	if (syscall == sys_open[arch])
		return isSysOpenAllowed(pid, allowed_files);

	constexpr int sys_lseek[2] = {
		19, // SYS_lseek - i386
		8 // SYS_lseek - x86_64
	};
	if (syscall == sys_lseek[arch] ||
		(arch == ARCH_i386 && syscall == 140)) // SYS__llseek
	{
		return isSysLseekAllowed(pid);
	}

	constexpr int sys_tgkill[2] = {
		270, // SYS_tgkill - i386
		234 // SYS_tgkill - x86_64
	};
	if (syscall == sys_tgkill[arch])
		return isSysTgkillAllowed(pid);

	return false;
}

template<class Callback, class Timer>
Sandbox::ExitStat Sandbox::Impl<Callback, Timer>::execute(
	const CStringView& exec, const std::vector<std::string>& args,
	const Options& opts, const CStringView& working_dir, Callback func)
{
	static_assert(std::is_base_of<CallbackBase, Callback>::value,
		"Callback has to derive from Sandbox::CallbackBase");

	// Set up error stream from tracee (and wait_for_syscall()) via pipe
	int pfd[2];
	if (pipe2(pfd, O_CLOEXEC) == -1)
		THROW("pipe()", error(errno));

	int cpid = fork();
	if (cpid == -1)
		THROW("fork()", error(errno));

	else if (cpid == 0) { // Child = tracee
		sclose(pfd[0]);
		runChild(exec, args, opts, working_dir, pfd[1], [=]{
			if (ptrace(PTRACE_TRACEME, 0, 0, 0))
				sendErrorMessage(pfd[1], errno, "ptrace(PTRACE_TRACEME)");
		});
	}

	sclose(pfd[1]);
	Closer close_pipe0(pfd[0]); // Guard closing of the pipe second end

	// Wait for tracee to be ready
	int status;
	waitpid(cpid, &status, 0);
	// If something went wrong
	if (WIFEXITED(status) || WIFSIGNALED(status))
		return ExitStat(status, 0, 0, receiveErrorMessage(status, pfd[0]));

	// Useful when exception is thrown
	bool tracee_is_dead_and_waited = false;
	auto kill_and_wait_tracee = [&] {
		if (!tracee_is_dead_and_waited) {
			kill(cpid, SIGKILL);
			waitpid(cpid, nullptr, 0);
		}
	};
	CallInDtor<decltype(kill_and_wait_tracee)> kill_and_wait_tracee_guard
		{kill_and_wait_tracee};

#ifndef PTRACE_O_EXITKILL
# define PTRACE_O_EXITKILL 0
#endif
	// Set up ptrace options
	if (ptrace(PTRACE_SETOPTIONS, cpid, 0, PTRACE_O_TRACESYSGOOD
		| PTRACE_O_EXITKILL))
	{
		THROW("ptrace(PTRACE_SETOPTIONS)", error(errno));
	}

	func.detectTraceeArchitecture(cpid);

	// Open /proc/{cpid}/statm
	FileDescriptor statm_fd {concat("/proc/", toStr(cpid), "/statm"),
		O_RDONLY};
	if (statm_fd == -1)
		THROW("open(/proc/{cpid}/statm)", error(errno));

	auto get_vm_size = [&] {
		(void)lseek(statm_fd, 0, SEEK_SET);
		std::array<char, 32> buff;
		ssize_t rc = read(statm_fd, buff.data(), buff.size() - 1);
		if (rc <= 0)
			THROW("read()", error(errno));

		buff[rc] = '\0';

		// Obtain value
		uint64_t vm_size = 0;
		for (int i = 0; i < 32 && isdigit(buff[i]); ++i)
			vm_size = vm_size * 10 + buff[i] - '0';

		DEBUG_SANDBOX(stdlog("get_vm_size: -> ", toStr(vm_size));)
		return vm_size;

	};

	uint64_t vm_size = 0; // In pages

	// Set up timer
	Timer timer(cpid, opts.time_limit);

	auto wait_for_syscall = [&]() -> int {
		for (;;) {
			(void)ptrace(PTRACE_SYSCALL, cpid, 0, 0); // Fail indicates that
			                                          // the tracee has just
			                                          // died
			waitpid(cpid, &status, 0);

			if (WIFSTOPPED(status)) {
				switch (WSTOPSIG(status)) {
				case SIGTRAP | 0x80:
					return 0; // We are in a syscall

				case SIGSTOP:
				case SIGTRAP:
				case SIGCONT:
					break;

				default:
					// Deliver intercepted signal to tracee
					(void)ptrace(PTRACE_CONT, cpid, 0, WSTOPSIG(status));
				}

			} else if (WIFEXITED(status) || WIFSIGNALED(status)) {
				tracee_is_dead_and_waited = true;
				return -1; // Tracee is dead now
			}
		}
	};

	for (;;) {
		auto exit_normally = [&]() -> ExitStat {
			uint64_t runtime = timer.stopAndGetRuntime();
			if (status)
				return ExitStat(status, runtime,
					vm_size * sysconf(_SC_PAGESIZE),
					receiveErrorMessage(status, pfd[0]));

			return ExitStat(status, runtime, vm_size * sysconf(_SC_PAGESIZE));
		};

		// Into syscall
		if (wait_for_syscall())
			return exit_normally();

		// Get syscall no.
	#ifdef __x86_64__
		long syscall_no = ptrace(PTRACE_PEEKUSER, cpid,
			offsetof(x86_64_user_regset, orig_rax), 0);
	#else
		long syscall_no = ptrace(PTRACE_PEEKUSER, cpid,
			offsetof(i386_user_regset, orig_eax), 0);
	#endif

		try {
			if (syscall_no < 0)
				THROW("failed to get syscall_no - ptrace(): ",
					toStr(syscall_no), error(errno));

			// Some useful debug
			DEBUG_SANDBOX(
				auto logSyscall = [&](bool with_result) {
					Registers regs;
					regs.getRegs(cpid);

					int64_t arg1 = (func.getArch() == ARCH_i386 ?
						int32_t(regs.uregs.i386_regs.ebx)
						: regs.uregs.x86_64_regs.rdi);
					int64_t arg2 = (func.getArch() == ARCH_i386 ?
						int32_t(regs.uregs.i386_regs.ecx)
						: regs.uregs.x86_64_regs.rsi);

					auto tmplog = stdlog('[', toStr(cpid), "] syscall: ",
						paddedString(toStr(syscall_no), 3), ": ",
						// syscall name
						(func.getArch() == ARCH_i386 ? x86_syscall_name
							: x86_64_syscall_name)[syscall_no], '(',
						toStr(arg1), ", ", toStr(arg2), ", ...)");

					if (with_result) {
						int64_t ret_val = (func.getArch() == ARCH_i386 ?
							int32_t(regs.uregs.i386_regs.eax)
							: regs.uregs.x86_64_regs.rax);
						tmplog(" -> ", toStr(ret_val));
					}
				};
			)

			// If syscall entry is allowed
			if (func.isSyscallEntryAllowed(cpid, syscall_no)) {
				// Syscall returns
				if (wait_for_syscall())
					return exit_normally();

				DEBUG_SANDBOX(logSyscall(true);)

				// Monitor for vm_size change
				if (func.getArch() == ARCH_i386
					? isIn(syscall_no, { // i386
						45, // SYS_brk
						90, // SYS_mmap
						163, // SYS_mremap
						192, // SYS_mmap2
					})
					: isIn(syscall_no, { // x86_64
						9, // SYS_mmap
						12, // SYS_brk
						25, // SYS_mremap
					}))
				{
					vm_size = std::max(vm_size, get_vm_size());
				}

				// Check syscall after returning
				if (func.isSyscallExitAllowed(cpid, syscall_no))
					continue;
			}

			DEBUG_SANDBOX(logSyscall(false);)

		} catch (...) {
			DEBUG_SANDBOX(stdlog(__FILE__ ":", toStr(__LINE__),
				": Caught exception");)

			// Exception after tracee is dead and waited
			if (tracee_is_dead_and_waited)
				throw;

			// Check whether the tracee is still controllable - may not become
			// a zombie in a moment (yes kernel has a minimal delay between
			// these two, that's waitpid(cpid, ..., WNOHANG) may raturn 0, even
			// though the tracee is not controllable anymore. The process state
			// in /proc/.../stat also may not be `Z`. That is why ptrace92) is
			// used here)
			errno = 0;
			(void)ptrace(PTRACE_PEEKUSER, cpid, 0, 0);
			// The tracee is no longer controllable
			if (errno == ESRCH) {
				kill(cpid, SIGKILL); // Make sure tracee is (will be) dead
				// Wait for tracee
				do {
					waitpid(cpid, &status, 0);
				} while (!WIFEXITED(status) && !WIFSIGNALED(status));
				// Return sandboxing results
				tracee_is_dead_and_waited = true;
				return exit_normally();
			}

			// Tracee is controllable or other error occured
			throw;
		}

		/* Syscall entry or exit is not allowed */

		uint64_t runtime = timer.stopAndGetRuntime();

		// Kill tracee
		kill(cpid, SIGKILL);
		// Wait for tracee
		do {
			waitpid(cpid, &status, 0);
		} while (!WIFEXITED(status) && !WIFSIGNALED(status));
		tracee_is_dead_and_waited = true;

		// Not allowed syscall
		std::string message = func.errorMessage();
		if (message.empty()) { // func has not left any message
			// Try to get syscall name
			CStringView syscall_name = (func.getArch() == ARCH_i386 ?
				x86_syscall_name : x86_64_syscall_name)[syscall_no];

			if (syscall_name.empty()) // Syscall not found
				message = concat("forbidden syscall ", toStr(syscall_no));
			else
				message = concat("forbidden syscall ", toStr(syscall_no), ": ", syscall_name, "()");
		}

		return ExitStat(status, runtime, vm_size * sysconf(_SC_PAGESIZE),
			message);
	}
}
