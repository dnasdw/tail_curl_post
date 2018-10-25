#ifndef TAIL_CURL_POST_H_
#define TAIL_CURL_POST_H_

#include <sdw.h>

class CTailCurlPost
{
public:
	CTailCurlPost();
	~CTailCurlPost();
	int GetExitCode() const;
	void Run();
private:
	void loadConfig();
	off_t bbFullFDAction(int a_nSrcFD, int a_nDestFD, off_t a_nSize) const;
	off_t bbCopyFDSize(int a_nSrcFD, int a_nDestFD, off_t a_nSize) const;
	int fullRead(int a_nFD, char* a_pBuffer, size_t a_uLength) const;
	int tailRead(int a_nFD, char* a_pBuffer, size_t a_uCount);
	vector<string> m_vFileName;
	string m_sPostUrl;
	bool m_bFromTop;
	int m_nExitCode;
	char* m_pTailBuffer;
	int* m_pFD;
};

#endif	// TAIL_CURL_POST_H_
