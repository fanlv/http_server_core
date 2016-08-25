#ifndef __TCPSERVER_H__
#define __TCPSERVER_H__

#include "Xtc.h"
#include "XtcSequence.h"
#include "XtcArray.h"
#include "TcpGroup.h"
#ifdef _OPENSSL
#include "openssl/ssl.h"
#endif


//ͳ��
typedef struct 
{
	int64_t send_size;//�ۼƷ����ֽ���
	int64_t recv_size;//�ۼƽ����ֽ���
	int64_t cmd_cnt;//�ۼƴ���ָ�����
	int64_t connect_cnt;//�ۼ��յ����Ӹ���
	int64_t session_cnt;//����Ӹ���
}STcpServerStatistics;//ͳ��


//session����ص��ӿ�
typedef CTcpSession* (*PTcpNewCallback)(void *param);



//TCP�Ự������
class CTcpServer
{
friend class CTcpGroup;
friend class CTcpSession;
public:
	CTcpServer();
	~CTcpServer();

	/*��������,timeout=-1��ʾ����ⳬʱ thread_num����0�Ļ����ա���������ȫ��dispatch������ɣ������Ҫ����
		need_response ��ʾһ�ʾ�Ҫһ��,model==1 ����ռʽ����(����֤ʱ����)��model==2,����ʽ����(��֤ʱ����)
	*/
	bool Initialize( uint32_t listen_ip, uint16_t listen_port, bool ssl_flag,int group_num,
			int32_t thread_num,int32_t model,bool need_response, PTcpNewCallback proc, void *param);
	//ֹͣ�����ͷ���Դ
	void Release();
	//�Ƿ��Ѿ���ʼ��
	bool IsInitialized();

	//�������ݣ����뻺���������أ��Ѽ����̰߳�ȫ
	bool PostData( STcpLink link, char_t *buf, int32_t size );

	//ȡ��ͳ����Ϣ
	void GetStatistics( STcpServerStatistics *sta );
	//���ͳ����Ϣ
	void AddStatistics( STcpServerStatistics& sta );


	uint32_t m_listen_ip;//������ַ������˳��
	uint16_t m_listen_port;//�����˿ڣ�����˳��
	bool m_ssl_flag;//�Ƿ�����ssl

protected:

#ifdef _OPENSSL
	SSL_CTX *m_ssl_ctx;//SSL����
#endif

	SOCKET m_listen_skt;//�����׽���
	STcpServerStatistics m_statistics;//ͳ����Ϣ

	CXtcArray<CTcpGroup*> m_groups;//���пͻ������ӵĹ����̳߳�

	void* m_thread;

	PTcpNewCallback m_proc;//SESSION���캯��
	void *m_param;

private:
	//����������
	static int32_t ListenProc( void* param, void* expend );
	//����������
	int32_t OnListen();

	uint64_t m_skt_idx;//socketΨһʶ�������,uint64_tһ����Ҳ�ò���
};


#endif //__TCPSERVER_H__
