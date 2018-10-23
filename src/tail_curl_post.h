#ifndef TAIL_CURL_POST_H_
#define TAIL_CURL_POST_H_

#include <sdw.h>

class CTailCurlPost
{
public:
	CTailCurlPost();
	~CTailCurlPost();
	void SetFromTop(bool a_bFromTop);
	bool IsFromTop() const;
	void SetExitcode(n32 a_nExitcode);
	n32 GetExitcode() const;
	int tail_read(int fd, char *buf, size_t count);
private:
	bool m_bFromTop;
	n32 m_nExitcode;
};

#endif	// TAIL_CURL_POST_H_
