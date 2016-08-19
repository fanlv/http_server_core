#ifndef __TCPSESSION_H__
#define __TCPSESSION_H__


#ifdef _OPENSSL
#include "openssl/ssl.h"
#endif
#include "Resource.h"


//HTTP�Ự���� 
class CTcpSession
{
friend class CTcpServer;
friend class CTcpGroup;
public:
	CTcpSession();
	~CTcpSession();

	//��ʼ����
	virtual void Start( void *group, STcpLink link );
	//ֹͣ����
	virtual void Stop();

	//����������־
	virtual void SetDead( bool flag );
	//�Ƿ������������ӶϿ���
	virtual bool IsDead( uint32_t now );

	//�������ݣ����������Լ�����
	virtual bool PostData( char_t *buf, int32_t size );

	//���ݰ�������
	virtual void OnPacket(char_t *buf, int32_t hlen, int32_t clen,uint32_t ip,uint16_t port,SOCKET skt,uint64_t idx,uint32_t now) ;
	//�������ݣ�����true��ʾ��socket���ˣ�false��ʾ��������
	virtual bool OnRecv( uint32_t now );
	//��������
	virtual void OnSend( char_t *buf, int32_t size, uint32_t now );
	//����ǰҪ�����һЩ�ص�
	virtual void HandleBeforeSend(SPacketHeader packet_header,char_t *buf,int32_t bufsize);

	//ȡ�����ڷ�����
	void *GetServer();
	//ȡ��������
	void *GetGroup();

	//���ó�ʱ����timerid
	void SetTimerId(uint32_t timerid);
	//ȡ��timerid
	uint32_t GetTimerId();
	//ȡ��m_close_flag
	bool GetCloseFlag();
	/*��socket��������*/
	bool Recv(void *ptr,char_t *buf,int32_t bufsize,bool is_ssl,int32_t *recvsize);
	/*����buffer ������������true  or ����false*/
	bool AnalysisBuf(char_t *buf,int32_t buflen,int32_t *pos);
	/*ssl��������*/
	bool HandShake();
public:
	void *m_group;
	STcpLink m_link;

protected:
#ifdef _OPENSSL
	SSL *m_ssl;
	bool m_handshaked;
#endif
	bool m_dead_flag;//�Ƿ��Ѿ����� 
	bool m_close_flag;//�Ƿ�ȴ��ر� 
	uint32_t m_act_tick;//�ϴ��շ�����ʱ�� 

	char_t *m_recv_buf;      //���ݽ��ջ����� 
	int32_t m_recv_bufsize; //���ݽ��ջ�������С 
	int32_t m_recv_datsize; //��ǰ���ݽ��ջ��������ѽ��յ������ݴ�С

	char_t *m_send_buf;      //���ݷ��ͻ����� 
	int32_t m_send_bufsize; //���ݷ��ͻ�������С 
	int32_t m_send_datsize; //�������ݴ�С

	uint32_t m_timer_id;//��ʱʱ��
};

#endif // __TCPSESSION_H__
