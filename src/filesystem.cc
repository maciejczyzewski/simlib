#include "../include/debug.h"
#include "../include/filesystem.h"
#include "../include/logger.h"
#include "../include/process.h"
#include "../include/utilities.h"

#include <dirent.h>

using std::array;
using std::string;
using std::unique_ptr;
using std::vector;

int openUnlinkedTmpFile(int flags) noexcept {
	int fd;
#ifdef O_TMPFILE
	fd = open("/tmp", O_TMPFILE | O_RDWR | flags, S_0600);
	if (fd != -1)
		return fd;

	// If errno == EINVAL, then fall back to mkostemp(3)
	if (errno != EINVAL)
		return -1;
#endif

	char name[] = "/tmp/tmp_unlinked_file.XXXXXX";
	umask(077); // Only owner can access this temporary file
	fd = mkostemp(name, flags);
	if (fd == -1)
		return -1;
	(void)unlink(name);
	return fd;
}

TemporaryDirectory::TemporaryDirectory(const CStringView& templ) {
	size_t size = templ.size();
	if (size > 0) {
		// Fill name_
		while (size && templ[size - 1] == '/')
			--size;

		name_.reset(new char[size + 2]);

		memcpy(name_.get(), templ.c_str(), size);
		name_.get()[size] = name_.get()[size + 1] = '\0';

		// Create directory with permissions (mode: 0700/rwx------)
		if (mkdtemp(name_.get()) == nullptr)
			THROW("Cannot create temporary directory");

		// name_ is absolute
		if (name_.get()[0] == '/')
			path_ = name();
		// name_ is not absolute
		else
			path_ = concat(getCWD(), name());

		// Make path_ absolute
		path_ = (abspath(path_) += '/');

		name_.get()[size] = '/';
	}
}

TemporaryDirectory::~TemporaryDirectory() {
#ifdef DEBUG
	if (exist() && remove_r(path_) == -1)
		errlog("Error: remove_r()", error(errno)); // We cannot throw because
		                                           // throwing from the
		                                           // destructor is UB
#else
	if (exist())
		(void)remove_r(path_); // Return value is ignored, we cannot throw
		                       // (because throwing from destructor is UB),
		                       // logging it is also not so good
#endif
}

int mkdir_r(string path, mode_t mode) noexcept {
	if (path.size() >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}

	// Add ending slash (if not exists)
	if (path.empty() || path.back() != '/')
		path += '/';

	size_t end = 1; // If there is a leading slash, it will be omitted
	int res = 0;
	while (end < path.size()) {
		while (path[end] != '/')
			++end;

		path[end] = '\0'; // Separate subpath
		res = mkdir(path.data(), mode);
		if (res == -1 && errno != EEXIST)
			return -1;

		path[end++] = '/';
	}

	return res;
}

/**
 * @brief Removes recursively directory @p pathname relative to a directory file
 *   descriptor @p dirfd
 *
 * @param dirfd directory file descriptor
 * @param pathname directory pathname (relative to @p dirfd)
 *
 * @return 0 on success, -1 on error
 *
 * @errors The same that occur for fstatat64(2), openat(2), unlinkat(2),
 *   fdopendir(3)
 */
static int __remove_rat(int dirfd, const CStringView& path) noexcept {
	int fd = openat(dirfd, path.c_str(), O_RDONLY | O_LARGEFILE | O_DIRECTORY
		| O_NOFOLLOW);
	if (fd == -1)
		return unlinkat(dirfd, path.c_str(), 0);

	DIR *dir = fdopendir(fd);
	if (dir == nullptr) {
		sclose(fd);
		return unlinkat(dirfd, path.c_str(), AT_REMOVEDIR);
	}

	dirent *file;
	while ((file = readdir(dir)))
		if (strcmp(file->d_name, ".") && strcmp(file->d_name, "..")) {
		#ifdef _DIRENT_HAVE_D_TYPE
			if (file->d_type == DT_DIR || file->d_type == DT_UNKNOWN) {
		#endif
				if (__remove_rat(fd, file->d_name)) {
					int ec = errno;
					closedir(dir);
					errno = ec;
					return -1;
				}
		#ifdef _DIRENT_HAVE_D_TYPE
			} else if (unlinkat(fd, file->d_name, 0)) {
				int ec = errno;
				closedir(dir);
				errno = ec;
				return -1;
			}
		#endif
		}

	closedir(dir);
	return unlinkat(dirfd, path.c_str(), AT_REMOVEDIR);
}

int remove_rat(int dirfd, const CStringView& path) noexcept {
	return __remove_rat(dirfd, path);
}

int removeDirContents_at(int dirfd, const CStringView& pathname) noexcept {
	int fd = openat(dirfd, pathname.c_str(), O_RDONLY | O_LARGEFILE
		| O_DIRECTORY | O_NOFOLLOW);
	if (fd == -1)
		return -1;

	DIR *dir = fdopendir(fd);
	if (dir == nullptr) {
		int ec = errno;
		sclose(fd);
		errno = ec;
		return -1;
	}

	dirent *file;
	while ((file = readdir(dir)))
		if (0 != strcmp(file->d_name, ".") && 0 != strcmp(file->d_name, "..")) {
		#ifdef _DIRENT_HAVE_D_TYPE
			if (file->d_type == DT_DIR || file->d_type == DT_UNKNOWN) {
		#endif
				if (__remove_rat(fd, file->d_name)) {
					int ec = errno;
					closedir(dir);
					errno = ec;
					return -1;
				}
		#ifdef _DIRENT_HAVE_D_TYPE
			} else if (unlinkat(fd, file->d_name, 0)) {
				int ec = errno;
				closedir(dir);
				errno = ec;
				return -1;
			}
		#endif
		}

	closedir(dir);
	return 0;
}

int blast(int infd, int outfd) noexcept {
	array<char, 65536> buff;
	ssize_t len, written;
	while (len = read(infd, buff.data(), buff.size()), len > 0 ||
		(len == -1 && errno == EINTR))
	{
		ssize_t pos = 0;
		while (pos < len) {
			written = write(outfd, buff.data() + pos, len - pos);
			if (written > 0)
				pos += written;
			else if (errno != EINTR)
				return -1;
		}
	}
	return 0;
}

int copy(const CStringView& src, const CStringView& dest) noexcept {
	int in = open(src.c_str(), O_RDONLY | O_LARGEFILE);
	if (in == -1)
		return -1;

	int out = open(dest.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_0644);
	if (out == -1) {
		sclose(in);
		return -1;
	}

	int res = blast(in, out);
	sclose(in);
	sclose(out);
	return res;
}

int copyat(int dirfd1, const CStringView& src, int dirfd2,
	const CStringView& dest) noexcept
{
	int in = openat(dirfd1, src.c_str(), O_RDONLY | O_LARGEFILE);
	if (in == -1)
		return -1;

	int out = openat(dirfd2, dest.c_str(), O_WRONLY | O_CREAT
		| O_TRUNC, S_0644);
	if (out == -1) {
		sclose(in);
		return -1;
	}

	int res = blast(in, out);
	sclose(in);
	sclose(out);
	return res;
}

/**
 * @brief Copies directory @p src to @p dest relative to the directory file
 *   descriptors
 *
 * @param dirfd1 directory file descriptor
 * @param src source directory (relative to dirfd1)
 * @param dirfd2 directory file descriptor
 * @param dest destination directory (relative to dirfd2)
 *
 * @return 0 on success, -1 on error
 *
 * @errors The same that occur for fstat64(2), openat(2), fdopendir(3),
 *   mkdirat(2), copyat()
 */
static int __copy_rat(int dirfd1, const CStringView& src, int dirfd2,
	const CStringView& dest) noexcept
{
	int src_fd = openat(dirfd1, src.c_str(), O_RDONLY |	O_LARGEFILE
		| O_DIRECTORY);
	if (src_fd == -1){
		if (errno == ENOTDIR)
			return copyat(dirfd1, src, dirfd2, dest);

		return -1;
	}

	// Do not use src permissions
	mkdirat(dirfd2, dest.c_str(), S_0755);

	int dest_fd = openat(dirfd2, dest.c_str(), O_RDONLY | O_LARGEFILE
		| O_DIRECTORY);
	if (dest_fd == -1) {
		sclose(src_fd);
		return -1;
	}

	DIR *src_dir = fdopendir(src_fd);
	if (src_dir == nullptr) {
		sclose(src_fd);
		sclose(dest_fd);
		return -1;
	}

	dirent *file;
	while ((file = readdir(src_dir)))
		if (0 != strcmp(file->d_name, ".") && 0 != strcmp(file->d_name, "..")) {
		#ifdef _DIRENT_HAVE_D_TYPE
			if (file->d_type == DT_DIR || file->d_type == DT_UNKNOWN)
		#endif
				__copy_rat(src_fd, file->d_name, dest_fd, file->d_name);
		#ifdef _DIRENT_HAVE_D_TYPE
			else
				copyat(src_fd, file->d_name, dest_fd, file->d_name);
		#endif
		}

	closedir(src_dir);
	sclose(dest_fd);
	return 0;
}

int copy_rat(int dirfd1, const CStringView& src, int dirfd2,
	const CStringView& dest) noexcept
{
	struct stat64 sb;
	if (fstatat64(dirfd1, src.c_str(), &sb, AT_SYMLINK_NOFOLLOW) == -1)
		return -1;

	if (S_ISDIR(sb.st_mode))
		return __copy_rat(dirfd1, src, dirfd2, dest);

	return copyat(dirfd1, src, dirfd2, dest);
}

int copy_r(const CStringView& src, const CStringView& dest, bool create_subdirs)
	noexcept
{
	if (!create_subdirs)
		return copy_rat(AT_FDCWD, src, AT_FDCWD, dest);

	size_t len = dest.size();
	if (len >= PATH_MAX) {
		errno = ENAMETOOLONG;
		return -1;
	}

	// Extract containing directory
	while (len && dest[len - 1] != '/')
		--len;

	// Ensure that parent directory exists
	mkdir_r(string {dest.data(), len});

	return copy_rat(AT_FDCWD, src, AT_FDCWD, dest);
}

int move(const CStringView& oldpath, const CStringView& newpath,
	bool create_subdirs) noexcept
{
	if (create_subdirs) {
		size_t x = newpath.rfind('/');
		if (x != CStringView::npos)
			mkdir_r(string {newpath.data(), x});
	}

	if (rename(oldpath.c_str(), newpath.c_str()) == -1) {
		if (errno == EXDEV && copy_r(oldpath, newpath, false) == 0)
			return remove_r(oldpath);

		return -1;
	}

	return 0;
}

int createFile(const CStringView& pathname, mode_t mode) noexcept {
	int fd = creat(pathname.c_str(), mode);
	if (fd == -1)
		return -1;

	return sclose(fd);
}

size_t readAll(int fd, void *buf, size_t count) noexcept {
	ssize_t k;
	size_t pos = 0;
	uint8_t *buff = static_cast<uint8_t*>(buf);
	do {
		k = read(fd, buff + pos, count - pos);
		if (k > 0)
			pos += k;
		else if (k == 0) {
			errno = 0; // No error
			return pos;

		} else if (errno != EINTR)
			return pos; // Error

	} while (pos < count);

	errno = 0; // No error
	return count;
}

size_t writeAll(int fd, const void *buf, size_t count) noexcept
{
	ssize_t k;
	size_t pos = 0;
	const uint8_t *buff = static_cast<const uint8_t*>(buf);
	do {
		k = write(fd, buff + pos, count - pos);
		if (k > 0)
			pos += k;
		else if (k == 0) {
			errno = 0; // No error
			return pos;

		} else if (errno != EINTR)
			return pos; // Error

	} while (pos < count);

	errno = 0; // No error
	return count;
}

string abspath(const StringView& path, size_t beg, size_t end, string curr_dir)
{
	if (end > path.size())
		end = path.size();

	// path begins with '/'
	if (beg < end && path[beg] == '/')
		curr_dir = '/';

	while (beg < end) {
		while (beg < end && path[beg] == '/')
			++beg;

		size_t next_slash = std::min(end, find(path, '/', beg, end));

		// If [beg, next_slash) == "."
		if (next_slash - beg == 1 && path[beg] == '.') {
			beg = next_slash;
			continue;

		// If [beg, next_slash) == ".."
		} else if (next_slash - beg == 2 && path[beg] == '.' &&
			path[beg + 1] == '.')
		{
			// Erase last path component
			size_t new_size = curr_dir.size();

			while (new_size > 0 && curr_dir[new_size - 1] != '/')
				--new_size;
			// If updated curr_dir != "/" erase trailing '/'
			if (new_size > 1)
				--new_size;
			curr_dir.resize(new_size);

			beg = next_slash;
			continue;
		}

		if (curr_dir.size() && curr_dir.back() != '/')
			curr_dir += '/';

		curr_dir.append(path.begin() + beg, path.begin() + next_slash);
		beg = next_slash;
	}

	return curr_dir;
}

string getFileContents(int fd, size_t bytes) {
	string res;
	array<char, 65536> buff;
	while (bytes > 0) {
		ssize_t len = read(fd, buff.data(), std::min(buff.size(), bytes));
		// Interrupted by signal
		if (len < 0 && errno == EINTR)
			continue;
		// EOF or error
		if (len <= 0)
			break;

		bytes -= len;
		res.append(buff.data(), len);
	}

	return res;
}

string getFileContents(int fd, off64_t beg, off64_t end) {
	array<char, 65536> buff;
	off64_t size = lseek64(fd, 0, SEEK_END);
	if (beg < 0)
		beg = std::max<off64_t>(size + beg, 0);
	if (size == (off64_t)-1 || beg > size)
		return "";

	// Change end to the valid value
	if (size < end || end < 0)
		end = size;

	if (end < beg)
		end = beg;
	// Reposition to beg
	if (beg != lseek64(fd, beg, SEEK_SET))
		return "";

	off64_t bytes_left = end - beg;
	string res;
	while (bytes_left > 0) {
		ssize_t len = read(fd, buff.data(),
			std::min<off64_t>(buff.size(), bytes_left));
		// Interrupted by signal
		if (len < 0 && errno == EINTR)
			continue;
		// EOF or error
		if (len <= 0)
			break;

		bytes_left -= len;
		res.append(buff.data(), len);
	}

	return res;
}

string getFileContents(const CStringView& file) {
	FileDescriptor fd;
	while (fd.open(file, O_RDONLY | O_LARGEFILE) == -1 && errno == EINTR) {}

	if (fd == -1)
		THROW("Failed to open file `", file, '`', error(errno));

	return getFileContents(fd);
}

string getFileContents(const CStringView& file, off64_t beg, off64_t end) {
	FileDescriptor fd;
	while (fd.open(file, O_RDONLY | O_LARGEFILE) == -1 && errno == EINTR) {}

	if (fd == -1)
		THROW("Failed to open file `", file, '`', error(errno));

	return getFileContents(fd, beg, end);
}

vector<string> getFileByLines(const CStringView& file, int flags, size_t first,
	size_t last)
{
	vector<string> res;

	FILE *f = fopen(file.c_str(), "r");
	if (f == nullptr)
		return res;

	char *buff = nullptr;
	size_t n = 0, line = 0;
	ssize_t read;
	// TODO: getline fails on '\0' ??? - check it out
	while ((read = getline(&buff, &n, f)) != -1) {
		if ((flags & GFBL_IGNORE_NEW_LINES) && buff[read - 1] == '\n')
			buff[read - 1] = '\0';

		if (line >= first && line < last)
			try {
				res.emplace_back(buff);
			} catch (...) {
				fclose(f);
				free(buff);
				throw;
			}

		++line;
	}

	fclose(f);
	free(buff);

	return res;
}

ssize_t putFileContents(const CStringView& file, const char* data,
	size_t len) noexcept
{
	FileDescriptor fd {file, O_WRONLY | O_CREAT | O_TRUNC, S_0644};
	if (fd == -1)
		return -1;

	if (len == size_t(-1))
		len = __builtin_strlen(data);

	return writeAll(fd, data, len);
}

string humanizeFileSize(uint64_t size) {
	constexpr uint64_t MIN_KB = 1ull << 10;
	constexpr uint64_t MIN_MB = 1ull << 20;
	constexpr uint64_t MIN_GB = 1ull << 30;
	constexpr uint64_t MIN_TB = 1ull << 40;
	constexpr uint64_t MIN_PB = 1ull << 50;
	constexpr uint64_t MIN_EB = 1ull << 60;
	constexpr uint64_t MIN_3DIGIT_KB = 102349ull;
	constexpr uint64_t MIN_3DIGIT_MB = 104805172ull;
	constexpr uint64_t MIN_3DIGIT_GB = 107320495309ull;
	constexpr uint64_t MIN_3DIGIT_TB = 109896187196212ull;
	constexpr uint64_t MIN_3DIGIT_PB = 112533595688920269ull;

	// Bytes
	if (size < MIN_KB)
		return (size == 1 ? "1 byte" : concat(toString(size), " bytes"));

	double dsize = size;
	// KB
	if (size < MIN_3DIGIT_KB)
		return toStr(dsize / MIN_KB, 1) + " KB";
	if (size < MIN_MB)
		return toStr(dsize / MIN_KB, 0) + " KB";
	// MB
	if (size < MIN_3DIGIT_MB)
		return toStr(dsize / MIN_MB, 1) + " MB";
	if (size < MIN_GB)
		return toStr(dsize / MIN_MB, 0) + " MB";
	// GB
	if (size < MIN_3DIGIT_GB)
		return toStr(dsize / MIN_GB, 1) + " GB";
	if (size < MIN_TB)
		return toStr(dsize / MIN_GB, 0) + " GB";
	// TB
	if (size < MIN_3DIGIT_TB)
		return toStr(dsize / MIN_TB, 1) + " TB";
	if (size < MIN_PB)
		return toStr(dsize / MIN_TB, 0) + " TB";
	// PB
	if (size < MIN_3DIGIT_PB)
		return toStr(dsize / MIN_PB, 1) + " PB";
	if (size < MIN_EB)
		return toStr(dsize / MIN_PB, 0) + " PB";
	// EB
	return toStr(dsize / MIN_EB, 1) + " EB";
}

namespace directory_tree {

void Node::__print(FILE *stream, string buff) const {
	fprintf(stream, "%s%s/\n", buff.c_str(), name_.c_str());

	// Update buffer
	if (buff.size() >= 4) {
		if (buff[buff.size() - 4] == '`')
			buff[buff.size() - 4] = ' ';
		buff.replace(buff.size() - 3, 2, "  ");
	}

	size_t dirs_len = dirs_.size(), files_len = files_.size();
	// Directories
	for (size_t i = 0; i < dirs_len; ++i)
		dirs_[i]->__print(stream, buff +
				(i + 1 == dirs_len && files_len == 0 ? "`-- " : "|-- "));

	// Files
	for (size_t i = 0; i < files_len; ++i)
		fprintf(stream, "%s%c-- %s\n", buff.c_str(),
				(i + 1 == files_len ? '`' : '|'), files_[i].c_str());
}

bool Node::__pathExists(StringView s) const noexcept {
	s.removeLeading('/');
	if (s.empty())
		return true;

	StringView x = s.extractLeading([](char c) { return (c != '/'); });
	if (s.empty())
		return fileExists(x);

	Node* d = dir(x);
	return (d ? d->__pathExists(s) : false);
}

Node* Node::dir(const StringView& pathname) const {
	if (dirs_.empty())
		return nullptr;

	auto down = dirs_.begin(), up = --dirs_.end();

	while (down < up) {
		auto mid = down + ((up - down) >> 1);
		if ((*mid)->name_ < pathname)
			down = ++mid;
		else
			up = mid;
	}

	return ((*down)->name_ == pathname ? down->get() : nullptr);
}

bool Node::removeDir(const StringView& pathname) {
	if (dirs_.empty())
		return false;

	auto down = dirs_.begin(), up = --dirs_.end();

	while (down < up) {
		auto mid = down + ((up - down) >> 1);
		if ((*mid)->name_ < pathname)
			down = ++mid;
		else
			up = mid;
	}

	if ((*down)->name_ != pathname)
		return false;

	dirs_.erase(down);
	return true;
}

bool Node::removeFile(const StringView& pathname) {
	auto it = binaryFind(files_, pathname);
	if (it == files_.end())
		return false;

	files_.erase(it);
	return true;
}

static unique_ptr<Node> __dumpDirectoryTreeAt(int dirfd,
	const CStringView& path)
{
	size_t len = path.size();
	while (len > 1 && path[len - 1] == '/')
		--len;

	unique_ptr<Node> root {new Node({path.data(), len})}; // Exception approved

	int fd = openat(dirfd, path.c_str(), O_RDONLY | O_LARGEFILE | O_DIRECTORY);
	if (fd == -1)
		return root;

	Directory dir {fdopendir(fd)};
	if (dir == nullptr) {
		sclose(fd);
		return root;
	}

	// Collect entities (recursively)
	forEachDirComponent(dir, [&](dirent* file) {
		if (file->d_type == DT_DIR)
			root->dirs_.emplace_back(__dumpDirectoryTreeAt(fd, file->d_name));
		else
			root->files_.emplace_back(file->d_name);
	});

	// Sort directories and files
	sort(root->dirs_, [](const unique_ptr<Node>& a, const unique_ptr<Node>& b) {
		return (a->name_ < b->name_);
	});
	sort(root->files_);

	return root;
}

unique_ptr<Node> dumpDirectoryTree(const CStringView& path) {
	if (!isDirectory(path))
		return nullptr;

	return __dumpDirectoryTreeAt(AT_FDCWD, path);
}

} // namespace directory_tree
