#include "tail_curl_post.h"

/*
* Read all of the supplied buffer from a file.
* This does multiple reads as necessary.
* Returns the amount read, or -1 on an error.
* A short read is returned on an end of file.
*/
int full_read(int fd, void *buf, size_t len)
{
	int cc;
	int total;

	total = 0;

	while (len)
	{
		cc = read(fd, buf, len);

		if (cc < 0)
		{
			if (total)
			{
				/* we already have some! */
				/* user can do another read to know the error code */
				return total;
			}
			return cc; /* read() returns -1 on failure. */
		}
		if (cc == 0)
			break;
		buf = ((char *)buf) + cc;
		total += cc;
		len -= cc;
	}

	return total;
}

/* Used by NOFORK applets (e.g. cat) - must not use xmalloc.
* size < 0 means "ignore write errors", used by tar --to-command
* size = 0 means "copy till EOF"
*/
static off_t bb_full_fd_action(int src_fd, int dst_fd, off_t size)
{
	int status = -1;
	off_t total = 0;
	bool continue_on_write_error = 0;
#define CONFIG_FEATURE_COPYBUF_KB 4
	char buffer[CONFIG_FEATURE_COPYBUF_KB * 1024];
	enum { buffer_size = sizeof(buffer) };

	if (size < 0)
	{
		size = -size;
		continue_on_write_error = 1;
	}

	if (src_fd < 0)
		goto out;

	while (true)
	{
		int rd;

		rd = read(src_fd, buffer, size > buffer_size ? buffer_size : size);
		if (rd < 0)
		{
			printf("read error\n");
			break;
		}
	read_ok:
		if (!rd)
		{ /* eof - all done */
			status = 0;
			break;
		}
		/* dst_fd == -1 is a fake, else... */
		if (dst_fd >= 0)
		{
			int wr = write(dst_fd, buffer, rd);
			if (wr < rd)
			{
				if (!continue_on_write_error)
				{
					printf("write error\n");
					break;
				}
				dst_fd = -1;
			}
		}
		total += rd;
		if (status < 0)
		{ /* if we aren't copying till EOF... */
			size -= rd;
			if (!size)
			{
				/* 'size' bytes copied - all done */
				status = 0;
				break;
			}
		}
	}
out:

	/* some environments don't have munmap(), hide it in #if */
	return status ? -1 : total;
}

off_t bb_copyfd_size(int fd1, int fd2, off_t size)
{
	if (size)
	{
		return bb_full_fd_action(fd1, fd2, size);
	}
	return 0;
}

CTailCurlPost::CTailCurlPost()
	: m_bFromTop(false)
	, m_nExitcode(EXIT_SUCCESS)
{
}

CTailCurlPost::~CTailCurlPost()
{
}

void CTailCurlPost::SetFromTop(bool a_bFromTop)
{
	m_bFromTop = a_bFromTop;
}

bool CTailCurlPost::IsFromTop() const
{
	return m_bFromTop;
}

void CTailCurlPost::SetExitcode(n32 a_nExitcode)
{
	m_nExitcode = a_nExitcode;
}

n32 CTailCurlPost::GetExitcode() const
{
	return m_nExitcode;
}

int CTailCurlPost::tail_read(int fd, char *buf, size_t count)
{
	int r;

	r = full_read(fd, buf, count);
	if (r < 0)
	{
		printf("read error\n");
		SetExitcode(EXIT_FAILURE);
	}

	return r;
}

#define header_fmt_str "\n==> %s <==\n"

int UMain(int argc, UChar* argv[])
{
	UPrintf(USTR("Hello world!\n"));
	CTailCurlPost g;

	unsigned count = 10;
	unsigned sleep_period = 1;
	const char *str_c, *str_n;

	char *tailbuf;
	size_t tailbufsize;
	unsigned header_threshhold = 1;
	unsigned nfiles;
	int i, opt;

	int *fds;
	const char *fmt;
	int prev_fd;

#define FOLLOW true
#define COUNT_BYTES false

# define FOLLOW_RETRY true

	char* pFileName[] = { "test.txt" };

	fds = static_cast<int*>(malloc(sizeof(fds[0]) * SDW_ARRAY_COUNT(pFileName)));
	if (fds == nullptr)
	{
		printf("out of memory\n");
		exit(EXIT_FAILURE);
	}
	nfiles = i = 0;
	do {
		int fd = open(pFileName[i], O_RDONLY, 0666);
		if (fd < 0 && !FOLLOW_RETRY)
		{
			g.SetExitcode(EXIT_FAILURE);
			continue;
		}
		fds[nfiles] = fd;
		pFileName[nfiles++] = pFileName[i];
	} while (++i < argc);

	if (!nfiles)
	{
		printf("no files\n");
		exit(EXIT_FAILURE);
	}

	/* prepare the buffer */
	tailbufsize = BUFSIZ;
	if (!g.IsFromTop() && COUNT_BYTES)
	{
		if (tailbufsize < count + BUFSIZ)
		{
			tailbufsize = count + BUFSIZ;
		}
	}
	/* tail -c1024m REGULAR_FILE doesn't really need 1G mem block.
	 * (In fact, it doesn't need ANY memory). So delay allocation.
	 */
	tailbuf = NULL;

	/* tail the files */

	fmt = header_fmt_str + 1; /* skip leading newline in the header on the first output */
	i = 0;
	do {
		char *buf;
		int taillen;
		int newlines_seen;
		unsigned seen;
		int nread;
		int fd = fds[i];

		if (fd < 0)
			continue; /* may happen with -F */

		if (nfiles > header_threshhold)
		{
			printf(fmt, pFileName[i]);
			fmt = header_fmt_str;
		}

		if (!g.IsFromTop())
		{
			off_t current = lseek(fd, 0, SEEK_END);
			if (current > 0)
			{
				unsigned off;
				if (COUNT_BYTES)
				{
					/* Optimizing count-bytes case if the file is seekable.
					 * Beware of backing up too far.
					 * Also we exclude files with size 0 (because of /proc/xxx) */
					if (count == 0)
						continue; /* showing zero bytes is easy :) */
					current -= count;
					if (current < 0)
						current = 0;
					lseek(fd, current, SEEK_SET);
					bb_copyfd_size(fd, fileno(stdout), count);
					continue;
				}
#if 1 /* This is technically incorrect for *LONG* strings, but very useful */
				/* Optimizing count-lines case if the file is seekable.
				 * We assume the lines are <64k.
				 * (Users complain that tail takes too long
				 * on multi-gigabyte files) */
				off = (count | 0xf); /* for small counts, be more paranoid */
				if (off > (INT_MAX / (64 * 1024)))
					off = (INT_MAX / (64 * 1024));
				current -= off * (64 * 1024);
				if (current < 0)
					current = 0;
				lseek(fd, current, SEEK_SET);
#endif
			}
		}

		if (!tailbuf)
			tailbuf = static_cast<char*>(malloc(tailbufsize));

		buf = tailbuf;
		taillen = 0;
		/* "We saw 1st line/byte".
		* Used only by +N code ("start from Nth", 1-based): */
		seen = 1;
		newlines_seen = 0;
		while ((nread = g.tail_read(fd, buf, tailbufsize - taillen)) > 0)
		{
			if (g.IsFromTop())
			{
				int nwrite = nread;
				if (seen < count)
				{
					/* We need to skip a few more bytes/lines */
					if (COUNT_BYTES)
					{
						nwrite -= (count - seen);
						seen += nread;
					}
					else
					{
						char *s = buf;
						do {
							--nwrite;
							if (*s++ == '\n' && ++seen == count)
							{
								break;
							}
						} while (nwrite);
					}
				}
				if (nwrite > 0)
					write(fileno(stdout), buf + nread - nwrite, nwrite);
			}
			else if (count)
			{
				if (COUNT_BYTES)
				{
					taillen += nread;
					if (taillen > (int)count)
					{
						memmove(tailbuf, tailbuf + taillen - count, count);
						taillen = count;
					}
				}
				else
				{
					int k = nread;
					int newlines_in_buf = 0;

					do { /* count '\n' in last read */
						k--;
						if (buf[k] == '\n')
						{
							newlines_in_buf++;
						}
					} while (k);

					if (newlines_seen + newlines_in_buf < (int)count)
					{
						newlines_seen += newlines_in_buf;
						taillen += nread;
					}
					else
					{
						int extra = (buf[nread - 1] != '\n');
						char *s;

						k = newlines_seen + newlines_in_buf + extra - count;
						s = tailbuf;
						while (k)
						{
							if (*s == '\n')
							{
								k--;
							}
							s++;
						}
						taillen += nread - (s - tailbuf);
						memmove(tailbuf, s, taillen);
						newlines_seen = count - extra;
					}
					if (tailbufsize < (size_t)taillen + BUFSIZ)
					{
						tailbufsize = taillen + BUFSIZ;
						tailbuf = static_cast<char*>(realloc(tailbuf, tailbufsize));
					}
				}
				buf = tailbuf + taillen;
			}
		} /* while (tail_read() > 0) */
		if (!g.IsFromTop())
		{
			write(fileno(stdout), tailbuf, taillen);
		}
	} while (++i < nfiles);
	prev_fd = fds[i - 1];

	tailbuf = static_cast<char*>(realloc(tailbuf, BUFSIZ));

	fmt = NULL;

	if (FOLLOW) while (1)
	{
#if SDW_PLATFORM == SDW_PLATFORM_WINDOWS
		Sleep(sleep_period * 1000);
#else
		sleep(sleep_period);
#endif

		i = 0;
		do {
			int nread;
			const char *filename = pFileName[i];
			int fd = fds[i];

			if (FOLLOW_RETRY)
			{
				struct stat sbuf, fsbuf;

				if (fd < 0
					|| fstat(fd, &fsbuf) < 0
					|| stat(filename, &sbuf) < 0
					|| fsbuf.st_dev != sbuf.st_dev
					|| fsbuf.st_ino != sbuf.st_ino
					) {
					int new_fd;

					if (fd >= 0)
						close(fd);
					new_fd = open(filename, O_RDONLY);
					if (new_fd >= 0) {
						printf("%s has %s; following end of new file\n",
							filename, (fd < 0) ? "appeared" : "been replaced"
							);
					}
					else if (fd >= 0) {
						printf("%s has become inaccessible\n", filename);
					}
					fds[i] = fd = new_fd;
				}
			}
			if (fd < 0)
				continue;
			if (nfiles > header_threshhold) {
				fmt = header_fmt_str;
			}
			for (;;) {
				/* tail -f keeps following files even if they are truncated */
				struct stat sbuf;
				/* /proc files report zero st_size, don't lseek them */
				if (fstat(fd, &sbuf) == 0 && sbuf.st_size > 0) {
					off_t current = lseek(fd, 0, SEEK_CUR);
					if (sbuf.st_size < current)
						lseek(fd, 0, SEEK_SET);
				}

				nread = g.tail_read(fd, tailbuf, BUFSIZ);
				if (nread <= 0)
					break;
				if (fmt && (fd != prev_fd)) {
					printf(fmt, filename);
					fmt = NULL;
					prev_fd = fd;
				}
				write(fileno(stdout), tailbuf, nread);
			}
		} while (++i < nfiles);
	} /* while (1) */

	if (true) {
		free(fds);
		free(tailbuf);
	}
	return g.GetExitcode();
}
