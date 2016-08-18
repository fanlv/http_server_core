#ifndef __TCPGROUP_H__
#define __TCPGROUP_H__

#include "Xtc.h"
#include "XtcSequence.h"
#include "XtcArray.h"
#include "XtcQueue.h"
#include "TcpSession.h"
#include "TcpCache.h"


#ifdef _OPENSSL
#include "openssl/ssl.h"
#endif

typedef struct STimer
{
		uint32_t timerID;//key
		//SOCKET fd;
		uint64_t skt_idx;
		void *position;
}STimer;

typedef struct STimeout
{
        uint32_t currentMS;//key
		uint32_t timerID;
}STimeout;

typedef struct SThreadArg
{
	CTcpCache<SPacket> m_innerCache;
	void *m_thread;
}SThreadArg;


#define MAX_WORK_THERAD_NUM 32

//TCP�Ự�� 
class CTcpGroup
{
friend class CTcpServer;
friend class CTcpSession;
public:
	CTcpGroup();
	~CTcpGroup();

	//��������,model = 1,��ռʽ,����֤ʱ���ԣ����Գ������cpu;model=2 ����ʽ����֤ʱ����,need_response---������Ч�������Ƿ�ظ�
	bool Start( void *server, int32_t cpu_idx,int thread_num ,int32_t cpu_num,int32_t model,bool need_response);
	//ֹͣ����
	void Stop();
	//�Ƿ��Ѿ���ʼ��
	bool IsStarted();

	void *GetServer();

	//���������
	void SetLinkBorn( STcpLink link );
	//Ҫ��link����
	bool SetLinkDead( STcpLink link );
	//Ѱ�����ӣ��Ѽ����̰߳�ȫ
	void* SearchLink( SOCKET *skt, CTcpSession **session );

	//�������ݣ����뻺���������أ��Ѽ����̰߳�ȫ
	bool PostData( SPacketHeader& packet_header, char_t *buf, int32_t size );

	uint32_t SetTimer(uint64_t skt_idx,void *position,int32_t timems = 60000);//��ʱĬ��һ����
	//�����¼�
	void SetEvent(int32_t fd,int ctrl,int event,void *ptr_param,int fd_param);
	//ȡ����ʱ���	
	void CancelTimer(uint32_t timerid);
	//����ʱ	
	void HandleTimeout();

	void GetSocket();
	bool AddRecvBuffer(SPacket& buffer);//����ռʽ��������,���ÿ��Լ��ٿ���
	bool AddRecvBufferToThreadQueue(SPacket& buffer);//������ʽ��ÿ���߳�ר���Ķ������ͣ����ÿ��Լ��ٿ���
	//��ȡ����ʽ
	int32_t GetModel();
	//CTcpCache m_innerCache;//���뻺����
	CTcpCache <SPacketHeader> m_outerCache;//���������
	bool m_need_response;//��û��ƥ��������ǣ��Ƿ���Ҫ���ͻ��˻ش�
	/*��������ʱʹ��*/
	char_t *m_recv_buf;
	int32_t m_recv_bufsize;

private:
	CXtcQueue<STcpLink> m_newQueue;//������
	CXtcSequence<CTcpSession*> m_sessions;//���л�ͻ���
	CXtcArray<CTcpSession*> m_dumps;//�ͻ������ӻ��ճ�

	void *m_epoll;//epoll���
	//void *m_work_thread[MAX_WORK_THERAD_NUM];//�����߳�
	CXtcArray<SThreadArg*> m_work_thread;
	void *m_dispatch_thread;//�����߳�
	void *m_server;//ָ������CTcpServer
	uint32_t m_lastTick;
	//int m_work_thread_num;
	void *m_rwlock;//m_sessions/m_outer_cacheר�ñ�����

	//��ʱ����ص�
	uint32_t m_timerID;
	CXtcSequence<STimer>m_timers;//��������key Ϊ timerID
	CXtcSequence<STimeout>m_timeout;//�������񣬸��ݾ���ʱ������
	//CXtcArray<SPacketHeader>m_dead_links;

	//ͨ�����socket����tcpserver accpet����socket
	int32_t m_server_accpet_socket;//server accpet ��soceket �������socketд����
	int32_t	m_group_accpet_socket;//��m_server_accpet_socket����ʹ��
	//CXtcQueue<SPacket> m_recvbufQueue;
	CTcpCache<SPacket> m_innerCache;

	int32_t m_control_fd;//�����fdд���ݱ���������Ҫ������
	int32_t m_notify_fd;//��m_control_fd����ʹ��

	void *m_inner_cache_mutex;//m_recvbufQueue ��
	pthread_cond_t  *m_inner_cache_cond;//��m_recvbuf_queue_mutex ����ʹ��

	int32_t m_model;//����ʽor��ռʽ

private:
	//��ռʽ���������� 
	static int32_t WorkProc( void* param, void* expend );
	//��ռʽ���������� 
	int32_t OnWork();

	//����ʽ���������� 
	static int32_t WorkProc1( void* param, void* expend );
	//����ʽ���������� 
	int32_t OnWork1(void* expend);

	//���ݼ�⺯�� 
	static int32_t DispatchProc( void* param, void* expend );
	//���ݼ�⺯�� 
	int32_t OnDispatch();

	//����������
	void* ActivateLink( STcpLink link );
	//ɱ�������� 
	void KillLink( void *position );
	//��fd�����ݶ���
	void ReadSocket(int32_t fd);

	//����ȽϺ��� 
//	static int32_t CompareLinkCallback(bool item1_is_key, void* item1, void* item2, void *param );
	static int32_t CompareSessionCallback(bool item1_is_key, void* item1, void* item2, void *param );

	//
	static int32_t CompareTaskByTimerIDCallback(bool item1_is_key, void* item1, void* item2, void *param );
	static int32_t CompareTaskByTimeMsCallback(bool item1_is_key, void* item1, void* item2, void *param );
};


#endif
