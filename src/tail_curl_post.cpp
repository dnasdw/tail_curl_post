#include "tail_curl_post.h"

CTailCurlPost::CTailCurlPost()
	: m_bFromTop(false)
	, m_nExitCode(EXIT_SUCCESS)
	, m_pTailBuffer(nullptr)
	, m_pFD(nullptr)
{
}

CTailCurlPost::~CTailCurlPost()
{
	if (m_pTailBuffer != nullptr)
	{
		free(m_pTailBuffer);
		m_pTailBuffer = nullptr;
	}
	if (m_pFD != nullptr)
	{
		free(m_pFD);
		m_pFD = nullptr;
	}
}

int CTailCurlPost::GetExitCode() const
{
	return m_nExitCode;
}

void CTailCurlPost::Run()
{
	char* pFileNames[] = { "/var/log/privoxy.log" };
	unsigned uCount = 10;
	unsigned uSleepPeriod = 1;
	int nHeaderThreshhold = 1;
#define FOLLOW true
#define COUNT_BYTES false
#define FOLLOW_RETRY true
	m_pFD = static_cast<int*>(malloc(sizeof(m_pFD[0]) * SDW_ARRAY_COUNT(pFileNames)));
	if (m_pFD == nullptr)
	{
		printf("out of memory\n");
		m_nExitCode = EXIT_FAILURE;
		return;
	}
	int i = 0;
	int nFiles = i;
	do {
		int nFD = open(pFileNames[i], O_RDONLY, 0666);
		if (nFD < 0 && !FOLLOW_RETRY)
		{
			m_nExitCode = EXIT_FAILURE;
			continue;
		}
		m_pFD[nFiles] = nFD;
		pFileNames[nFiles++] = pFileNames[i];
	} while (++i < SDW_ARRAY_COUNT(pFileNames));
	if (nFiles == 0)
	{
		printf("no files\n");
		m_nExitCode = EXIT_FAILURE;
		return;
	}
	/* prepare the buffer */
	size_t uTailBufferSize = BUFSIZ;
	if (!m_bFromTop && COUNT_BYTES)
	{
		if (uTailBufferSize < uCount + BUFSIZ)
		{
			uTailBufferSize = uCount + BUFSIZ;
		}
	}
	/* tail -c1024m REGULAR_FILE doesn't really need 1G mem block.
	 * (In fact, it doesn't need ANY memory). So delay allocation.
	 */
	m_pTailBuffer = NULL;
	/* tail the files */
#define header_fmt_str "\n==> %s <==\n"
	const char* pFormat = header_fmt_str + 1; /* skip leading newline in the header on the first output */
	i = 0;
	do {
		int nRead = 0;
		int nFD = m_pFD[i];
		if (nFD < 0)
		{
			continue; /* may happen with -F */
		}
		if (nFiles > nHeaderThreshhold)
		{
			printf(pFormat, pFileNames[i]);
			pFormat = header_fmt_str;
		}
		if (!m_bFromTop)
		{
			off_t nCurrent = lseek(nFD, 0, SEEK_END);
			if (nCurrent > 0)
			{
				if (COUNT_BYTES)
				{
					/* Optimizing count-bytes case if the file is seekable.
					 * Beware of backing up too far.
					 * Also we exclude files with size 0 (because of /proc/xxx) */
					if (uCount == 0)
					{
						continue; /* showing zero bytes is easy :) */
					}
					nCurrent -= uCount;
					if (nCurrent < 0)
					{
						nCurrent = 0;
					}
					lseek(nFD, nCurrent, SEEK_SET);
					bbCopyFDSize(nFD, fileno(stdout), uCount);
					continue;
				}
#if 1 /* This is technically incorrect for *LONG* strings, but very useful */
				/* Optimizing count-lines case if the file is seekable.
				 * We assume the lines are <64k.
				 * (Users complain that tail takes too long
				 * on multi-gigabyte files) */
				unsigned uOff = (uCount | 0xf); /* for small counts, be more paranoid */
				if (uOff > (INT_MAX / (64 * 1024)))
				{
					uOff = (INT_MAX / (64 * 1024));
				}
				nCurrent -= uOff * (64 * 1024);
				if (nCurrent < 0)
				{
					nCurrent = 0;
				}
				lseek(nFD, nCurrent, SEEK_SET);
#endif
			}
		}
		if (m_pTailBuffer == nullptr)
		{
			m_pTailBuffer = static_cast<char*>(malloc(uTailBufferSize));
		}
		char* pBuffer = m_pTailBuffer;
		int nTailLength = 0;
		/* "We saw 1st line/byte".
		 * Used only by +N code ("start from Nth", 1-based): */
		unsigned uSeen = 1;
		int nNewlinesSeen = 0;
		while ((nRead = tailRead(nFD, pBuffer, uTailBufferSize - nTailLength)) > 0)
		{
			if (m_bFromTop)
			{
				int nWrite = nRead;
				if (uSeen < uCount)
				{
					/* We need to skip a few more bytes/lines */
					if (COUNT_BYTES)
					{
						nWrite -= (uCount - uSeen);
						uSeen += nRead;
					}
					else
					{
						char* pString = pBuffer;
						do {
							--nWrite;
							if (*pString++ == '\n' && ++uSeen == uCount)
							{
								break;
							}
						} while (nWrite != 0);
					}
				}
				if (nWrite > 0)
				{
					write(fileno(stdout), pBuffer + nRead - nWrite, nWrite);
				}
			}
			else if (uCount != 0)
			{
				if (COUNT_BYTES)
				{
					nTailLength += nRead;
					if (nTailLength > static_cast<int>(uCount))
					{
						memmove(m_pTailBuffer, m_pTailBuffer + nTailLength - uCount, uCount);
						nTailLength = uCount;
					}
				}
				else
				{
					int k = nRead;
					int nNewlinesInBuffer = 0;
					do { /* count '\n' in last read */
						k--;
						if (pBuffer[k] == '\n')
						{
							nNewlinesInBuffer++;
						}
					} while (k != 0);
					if (nNewlinesSeen + nNewlinesInBuffer < static_cast<int>(uCount))
					{
						nNewlinesSeen += nNewlinesInBuffer;
						nTailLength += nRead;
					}
					else
					{
						int nExtra = pBuffer[nRead - 1] != '\n' ? 1 : 0;
						k = nNewlinesSeen + nNewlinesInBuffer + nExtra - uCount;
						char* pString = m_pTailBuffer;
						while (k != 0)
						{
							if (*pString == '\n')
							{
								k--;
							}
							pString++;
						}
						nTailLength += nRead - (pString - m_pTailBuffer);
						memmove(m_pTailBuffer, pString, nTailLength);
						nNewlinesSeen = uCount - nExtra;
					}
					if (uTailBufferSize < (size_t)nTailLength + BUFSIZ)
					{
						uTailBufferSize = nTailLength + BUFSIZ;
						m_pTailBuffer = static_cast<char*>(realloc(m_pTailBuffer, uTailBufferSize));
					}
				}
				pBuffer = m_pTailBuffer + nTailLength;
			}
		} /* while (tail_read() > 0) */
		if (!m_bFromTop)
		{
			write(fileno(stdout), m_pTailBuffer, nTailLength);
		}
	} while (++i < nFiles);
	int nPrevFD = m_pFD[i - 1];
	m_pTailBuffer = static_cast<char*>(realloc(m_pTailBuffer, BUFSIZ));
	pFormat = NULL;
	if (FOLLOW)
	{
		while (true)
		{
#if SDW_PLATFORM == SDW_PLATFORM_WINDOWS
			Sleep(uSleepPeriod * 1000);
#else
			sleep(uSleepPeriod);
#endif
			i = 0;
			do {
				const char* pFileName = pFileNames[i];
				int nFD = m_pFD[i];
				if (FOLLOW_RETRY)
				{
					struct stat sbuf = {}, fsbuf = {};
					if (nFD < 0
						|| fstat(nFD, &fsbuf) < 0
						|| stat(pFileName, &sbuf) < 0
#if SDW_PLATFORM != SDW_PLATFORM_WINDOWS
						|| fsbuf.st_dev != sbuf.st_dev
#endif
						|| fsbuf.st_ino != sbuf.st_ino
						)
					{
						if (nFD >= 0)
						{
							close(nFD);
						}
						int nNewFD = open(pFileName, O_RDONLY);
						if (nNewFD >= 0)
						{
							printf("%s has %s; following end of new file\n",
								pFileName, (nFD < 0) ? "appeared" : "been replaced"
								);
						}
						else if (nFD >= 0)
						{
							printf("%s has become inaccessible\n", pFileName);
						}
						m_pFD[i] = nFD = nNewFD;
					}
				}
				if (nFD < 0)
				{
					continue;
				}
				if (nFiles > nHeaderThreshhold)
				{
					pFormat = header_fmt_str;
				}
				for (;;)
				{
					/* tail -f keeps following files even if they are truncated */
					struct stat sbuf = {};
					/* /proc files report zero st_size, don't lseek them */
					if (fstat(nFD, &sbuf) == 0 && sbuf.st_size > 0)
					{
						off_t nCurrent = lseek(nFD, 0, SEEK_CUR);
						if (sbuf.st_size < nCurrent)
						{
							lseek(nFD, 0, SEEK_SET);
						}
					}
					int nRead = tailRead(nFD, m_pTailBuffer, BUFSIZ);
					if (nRead <= 0)
					{
						break;
					}
					if (pFormat != nullptr && (nFD != nPrevFD))
					{
						printf(pFormat, pFileName);
						pFormat = NULL;
						nPrevFD = nFD;
					}
					write(fileno(stdout), m_pTailBuffer, nRead);
				}
			} while (++i < nFiles);
		} /* while (1) */
	}
}

/* Used by NOFORK applets (e.g. cat) - must not use xmalloc.
 * size < 0 means "ignore write errors", used by tar --to-command
 * size = 0 means "copy till EOF"
 */
off_t CTailCurlPost::bbFullFDAction(int a_nSrcFD, int a_nDestFD, off_t a_nSize) const
{
	int nStatus = -1;
	off_t nTotal = 0;
	bool bContinueOnWriteError = false;
#define CONFIG_FEATURE_COPYBUF_KB 4
	char cBuffer[CONFIG_FEATURE_COPYBUF_KB * 1024];
	enum
	{
		buffer_size = sizeof(cBuffer)
	};
	if (a_nSize < 0)
	{
		a_nSize = -a_nSize;
		bContinueOnWriteError = true;
	}
	if (a_nSrcFD < 0)
	{
		goto out;
	}
	while (true)
	{
		int nRead = read(a_nSrcFD, cBuffer, a_nSize > buffer_size ? buffer_size : a_nSize);
		if (nRead < 0)
		{
			printf("read error\n");
			break;
		}
		if (nRead == 0)
		{ /* eof - all done */
			nStatus = 0;
			break;
		}
		/* dst_fd == -1 is a fake, else... */
		if (a_nDestFD >= 0)
		{
			int nWrite = write(a_nDestFD, cBuffer, nRead);
			if (nWrite < nRead)
			{
				if (!bContinueOnWriteError)
				{
					printf("write error\n");
					break;
				}
				a_nDestFD = -1;
			}
		}
		nTotal += nRead;
		if (nStatus < 0)
		{ /* if we aren't copying till EOF... */
			a_nSize -= nRead;
			if (a_nSize == 0)
			{
				/* 'size' bytes copied - all done */
				nStatus = 0;
				break;
			}
		}
	}
out:
	/* some environments don't have munmap(), hide it in #if */
	return nStatus != 0 ? -1 : nTotal;
}

off_t CTailCurlPost::bbCopyFDSize(int a_nSrcFD, int a_nDestFD, off_t a_nSize) const
{
	if (a_nSize != 0)
	{
		return bbFullFDAction(a_nSrcFD, a_nDestFD, a_nSize);
	}
	return 0;
}

/*
 * Read all of the supplied buffer from a file.
 * This does multiple reads as necessary.
 * Returns the amount read, or -1 on an error.
 * A short read is returned on an end of file.
 */
int CTailCurlPost::fullRead(int a_nFD, char* a_pBuffer, size_t a_uLength) const
{
	int nTotal = 0;
	while (a_uLength != 0)
	{
		int nCharCount = read(a_nFD, a_pBuffer, a_uLength);
		if (nCharCount < 0)
		{
			if (nTotal != 0)
			{
				/* we already have some! */
				/* user can do another read to know the error code */
				return nTotal;
			}
			return nCharCount; /* read() returns -1 on failure. */
		}
		if (nCharCount == 0)
		{
			break;
		}
		a_pBuffer = a_pBuffer + nCharCount;
		nTotal += nCharCount;
		a_uLength -= nCharCount;
	}
	return nTotal;
}

int CTailCurlPost::tailRead(int a_nFD, char* a_pBuffer, size_t a_uCount)
{
	int nRead = fullRead(a_nFD, a_pBuffer, a_uCount);
	if (nRead < 0)
	{
		printf("read error\n");
		m_nExitCode = EXIT_FAILURE;
	}
	return nRead;
}

int main(int argc, char* argv[])
{
	CTailCurlPost g;
	g.Run();
	return g.GetExitCode();
}
