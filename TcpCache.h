#ifndef __TCPCACHE_H__
#define __TCPCACHE_H__

//#include "TcpSession.h"

template<class TYPE>
class CTcpCache
{
public:
	CTcpCache();
	~CTcpCache();

	//��ʼ��
	void Create( int32_t bufsize );
	//�ͷ�
	void Destroy();

	//�ʵ���Ϣ
	bool Post( TYPE& type, char_t *buf, int32_t size );
	//��ȡ��Ϣ
	int32_t Read( TYPE *type, char_t *buf, int32_t size );
	//���
	void Clear();

	bool IsEmpty();

protected:
	uchar_t *m_buf;
	int32_t m_bufsize;
	int32_t m_head;
	int32_t m_tail;
};

#endif //__TCPCACHE_H__
