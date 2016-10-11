#include "stdafx.h"
#include "osl_epoll.h"
#include "osl_socket.h"
#include "osl_log.h"
#include "osl_thread.h"
#include "osl_mem.h"
#include "osl_url.h"
#include "osl_mutex.h"
#include "TcpServer.h"
#include "TcpGroup.h"
#include "TcpCache.cpp"
#ifdef _OPENSSL
#include "openssl/ssl.h"
#endif
#include "main.h"


//TCP�Ự��
CTcpGroup::CTcpGroup()
{
	m_epoll = NULL;
	m_dispatch_thread = NULL;
	//memset(m_work_thread,0,sizeof(m_dispatch_thread));
	m_server = NULL;
	m_lastTick = 0;
	m_rwlock = NULL;
	m_inner_cache_mutex = NULL;
	m_inner_cache_cond = NULL;
	//ͨ�����socket����tcpserver accpet����socket
	m_server_accpet_socket = -1;//server accpet ��soceket �������socketд����
	m_group_accpet_socket = -1;//��m_server_accpet_socket����ʹ��
	m_control_fd = -1;//�����fdд���ݱ���������Ҫ������
	m_notify_fd = -1;//��m_control_fd����ʹ��
	//m_recv_buf = NULL;

	m_sessions.SetCompareCallback( CTcpGroup::CompareSessionCallback, this );
	//m_recvLinks.SetCompareCallback( CTcpGroup::CompareLinkCallback, this );
	//m_sendLinks.SetCompareCallback( CTcpGroup::CompareLinkCallback, this );


	//��ʱ������
	m_timerID = 0;
	m_timers.SetCompareCallback(CTcpGroup::CompareTaskByTimerIDCallback,this);
	m_timeout.SetCompareCallback(CTcpGroup::CompareTaskByTimeMsCallback,this);
	m_need_response = false;
}


CTcpGroup::~CTcpGroup()
{
	Stop();
}


//��������
bool CTcpGroup::Start( void *server, int32_t cpu_idx,int32_t thread_num ,int32_t cpu_num,int32_t model,bool need_response)
{
	SThreadArg *thread_arg;
	int32_t work_thread_num;
	static int32_t thread_cpu_idx = 0;
	int fd[2];
	
	m_newQueue.Create(8196);
	m_outerCache.Create( g_settings.group_cache_size*1024*1024 );//�ⲿ���ݷ��ͻ�����

	m_epoll = osl_epoll_create( 65536 );
	m_server= server;
	m_lastTick = 0;
	m_need_response = need_response;
	m_recv_bufsize = 65536;
	m_recv_buf = (char_t*)malloc(m_recv_bufsize);
	
	if ( 0 != socketpair(AF_UNIX, SOCK_STREAM, 0, fd) )
	 	return false;

	int n = 65536;
	setsockopt(fd[0], SOL_SOCKET, SO_SNDBUF, &n, sizeof(n));
	fcntl(fd[0], F_SETFL, O_RDWR|O_NONBLOCK);

	setsockopt(fd[1], SOL_SOCKET, SO_RCVBUF, &n, sizeof(n));
	fcntl(fd[1], F_SETFL, O_RDWR|O_NONBLOCK);

	m_server_accpet_socket   = fd[0];
	m_group_accpet_socket    = fd[1]; 

	SetEvent(m_group_accpet_socket,OSL_EPOLL_CTL_ADD,OSL_EPOLL_IN,NULL,m_group_accpet_socket);

	if ( 0 != socketpair(AF_UNIX, SOCK_STREAM, 0, fd) )
	 	return false;

	setsockopt(fd[0], SOL_SOCKET, SO_SNDBUF, &n, sizeof(n));
	fcntl(fd[0], F_SETFL, O_RDWR|O_NONBLOCK);

	setsockopt(fd[1], SOL_SOCKET, SO_RCVBUF, &n, sizeof(n));
	fcntl(fd[1], F_SETFL, O_RDWR|O_NONBLOCK);

	m_control_fd   = fd[0];
	m_notify_fd    = fd[1]; 

	SetEvent(m_notify_fd,OSL_EPOLL_CTL_ADD,OSL_EPOLL_IN,NULL,m_notify_fd);		
	m_rwlock = osl_rwlock_create();

	work_thread_num = thread_num < MAX_WORK_THERAD_NUM ? thread_num : MAX_WORK_THERAD_NUM;
	if(model != 1 && model != 2)
	{
		model = 1;//Ĭ�Ͼ�����ռʽ
	}
	/*���work_thread_num ������0 ���������е�������Dispatch �̴߳���*/
	m_model = model;
	if(model == 1)
	{
		//��ռʽ
		if(work_thread_num > 0)
		{
			m_innerCache.Create(g_settings.group_cache_size*1024*1024);//
			m_inner_cache_mutex = osl_mutex_create();//m_innerCache ��
			m_inner_cache_cond = (pthread_cond_t*)malloc(sizeof(pthread_cond_t));//��m_inner_cache_mutex ����ʹ��
			pthread_cond_init( m_inner_cache_cond, NULL );

			for(int i = 0;i< work_thread_num ;i++)
			{
				thread_arg = (SThreadArg*)malloc(sizeof(SThreadArg));
				memset(thread_arg,0,sizeof(SThreadArg));
				thread_arg->m_thread = osl_thread_create( "tTcpGroup", 100, 3*1024*1024, CTcpGroup::WorkProc, this, NULL );
				if(thread_arg->m_thread)
				{
					m_work_thread.Add(thread_arg);
					osl_thread_bind_cpu( thread_arg->m_thread, (thread_cpu_idx++)%cpu_num );
					osl_thread_resume(thread_arg->m_thread);
				}

				osl_log_debug("func:%s start thread num:%d thread:%x\n",__func__,i,thread_arg->m_thread);
			}
		}
	}
	else if(model == 2)
	{
		//����ʽ
		if(work_thread_num > 0)
		{
			for(int i = 0;i< work_thread_num ;i++)
			{
				thread_arg = (SThreadArg*)malloc(sizeof(SThreadArg));
				memset(thread_arg,0,sizeof(SThreadArg));
				//thread_arg->m_innerCache = new CTcpCache<SPacket>();
				thread_arg->m_innerCache.Create(g_settings.group_cache_size*1024*1024);//
				thread_arg->m_thread = osl_thread_create( "tTcpGroup1", 100, 3*1024*1024, CTcpGroup::WorkProc1, this, thread_arg );
				if(thread_arg->m_thread)
				{
					m_work_thread.Add(thread_arg);
					osl_thread_bind_cpu( thread_arg->m_thread, (thread_cpu_idx++)%cpu_num );
					osl_thread_resume(thread_arg->m_thread);
				}

				osl_log_debug("func:%s start thread num:%d thread:%x\n",__func__,i,thread_arg->m_thread);
			}
		}
	}

	
	m_dispatch_thread = osl_thread_create( "tTcpDispatch", 100, 3*1024*1024, CTcpGroup::DispatchProc, this, NULL );
	if ( m_dispatch_thread )
	{
		osl_thread_bind_cpu( m_dispatch_thread, cpu_idx );
		osl_thread_resume( m_dispatch_thread );
		//return true;
	}
	else
	{
		osl_log_debug("[CTcpGroup][Start] error !!!\n");
	}
	
	return true;
}

//ֹͣ
void CTcpGroup::Stop()
{
	STcpLink link;
	void *position;
	CTcpSession *session;
	SPacket packet;

	if( m_dispatch_thread )
	{
		osl_thread_destroy( m_dispatch_thread, -1 );
		m_dispatch_thread = NULL;
	}
	
	for(int i=0;i<m_work_thread.GetSize();i++)
	{
		if(m_work_thread[i]->m_thread)
		{
			osl_thread_destroy( m_work_thread[i]->m_thread, -1 );
			m_work_thread[i]->m_thread = NULL;
		}
	}

	for(int i=0;i<m_work_thread.GetSize();i++)
	{
		if(m_work_thread[i])
		{
			m_work_thread[i]->m_innerCache.Destroy();
		}

		free(m_work_thread[i]);
		m_work_thread[i] = NULL;
	}

	m_work_thread.RemoveAll();

	if ( m_rwlock )
	{
		osl_rwlock_destroy( m_rwlock );
		m_rwlock = NULL;
	}

	m_outerCache.Destroy();

	while(m_newQueue.Read(&link))
	{
		if (link.skt != -1 )
			osl_socket_destroy( link.skt );
	}
	m_newQueue.Destroy();

	position = m_sessions.GetFirst( &session );
	while( position )
	{
		session->Stop();
		delete session;
		position = m_sessions.GetNext( &session, position );
	}
	m_sessions.RemoveAll();

	if( m_epoll )
	{
		osl_epoll_destroy( m_epoll );
		m_epoll = NULL;
	}

	if(m_inner_cache_cond)
	{
		pthread_cond_destroy( m_inner_cache_cond );
		free(m_inner_cache_cond);
		m_inner_cache_cond = NULL;
	}

	if(m_inner_cache_mutex)
	{
		osl_mutex_destroy(m_inner_cache_mutex);
		m_inner_cache_mutex = NULL;
	}

	if(-1 != m_server_accpet_socket)
	{
		close(m_server_accpet_socket);
		m_server_accpet_socket = -1;
	}

	if(-1 != m_control_fd)
	{
		close(m_control_fd);
		m_control_fd = -1;
	}

	if(-1 != m_group_accpet_socket)
	{
		close(m_group_accpet_socket);
		m_group_accpet_socket = -1;
	}

	if(-1 != m_notify_fd)
	{
		close(m_notify_fd);
		m_notify_fd = -1;
	}

	
	if(m_recv_buf)
	{	
		free(m_recv_buf);
		m_recv_buf = NULL;
	}

	m_innerCache.Destroy();

	m_timers.RemoveAll();
	m_timeout.RemoveAll();
}


//�Ƿ��Ѿ���ʼ��
bool CTcpGroup::IsStarted()
{
	return m_epoll != NULL;
}

void* CTcpGroup::GetServer()
{
	return m_server;
}

//���������
void CTcpGroup::SetLinkBorn( STcpLink link )
{
	osl_log_debug("func :%s post skt:%d \n",__func__,link.skt);
	m_newQueue.Post( link );
	char buf = 'Z';
	int ret = write(m_server_accpet_socket,&buf,1);
	if(ret != 1)
	{
		if ( errno != EAGAIN )
		{
			printf ( "NOTIFY: write data failed fd %i size %i errno %i '%s'",
				m_server_accpet_socket, ret, errno, strerror(errno) );
		}
	}
}

//�������ݣ����뻺���������أ��Ѽ����̰߳�ȫ
bool CTcpGroup::PostData( SPacketHeader& packet_header, char_t *buf, int32_t size )
{
	//printf("PostData:%s\n",buf);
	int ret,ret1;

	if(size > 65536)
	{
		//����65536 ��ְ�����,ע�����ú�PACKET_START �� PACKET_END
		osl_log_debug("[%s] size:%d too much,please Packet transmission\n",__func__,size);
		return false;
	}
	
	osl_rwlock_write_lock(m_rwlock);
	ret = m_outerCache.Post(packet_header, buf, size);
	osl_rwlock_write_unlock(m_rwlock);
	
	char bufchar = 'Z';
	ret1 = write(m_control_fd,&bufchar,1);
	if(ret1 != 1)
	{
		if ( errno != EAGAIN )
		{
			osl_log_debug ( "NOTIFY: write data failed fd %i size %i errno %i '%s'",
				m_control_fd, ret, errno, strerror(errno) );
		}
	}

	return ret;
}

//�����¼�
void CTcpGroup::SetEvent(int32_t fd,int ctrl,int event,void *ptr_param,int fd_param)
{
	SEpollEvent ev;
	memset(&ev, 0, sizeof(ev));
	ev.events = event ;//LT �� ET �����ԣ�LT������һЩ����Ϊϵͳ��ƿ����������
	if(ptr_param)
		ev.data.ptr = ptr_param;
	else if(fd_param > 0)
		ev.data.fd = fd_param;
	
	osl_epoll_ctl( m_epoll, ctrl, fd, &ev );
}

bool CTcpGroup::AddRecvBuffer(SPacket& packet)
{
	bool ret = false;
	//osl_log_debug("func:%s skt:%d size:%d\n",__func__,packet.header.link.skt,packet.len);
	//ret = m_recvbufQueue.Post(packet);
	if(packet.len > 65535)
	{
		osl_log_debug("func:%s skt_idx:%llu size:%d overflow\n",__func__,packet.header.link.skt_idx,packet.len);
		return false;
	}
	ret = m_innerCache.Post(packet,packet.buf,packet.len);
	if(ret)
		pthread_cond_signal( m_inner_cache_cond );

	return ret;
}

bool CTcpGroup::AddRecvBufferToThreadQueue(SPacket& packet)//������ʽ��ÿ���߳�ר���Ķ�������
{
	bool ret = false;
	int32_t idx = packet.header.link.skt_idx % m_work_thread.GetSize();
	SThreadArg *arg =  m_work_thread[idx];

	if(packet.len > 65535)
	{
		osl_log_debug("func:%s skt_idx:%llu size:%d overflow\n",__func__,packet.header.link.skt_idx,packet.len);
		return false;
	}
	
	if(arg)
	{
		ret = arg->m_innerCache.Post( packet,packet.buf,packet.len );
	}

	return ret;
}

// 
int32_t CTcpGroup::DispatchProc( void* param, void* expend )
{
	return ((CTcpGroup *)param)->OnDispatch();
}

// 
int32_t CTcpGroup::OnDispatch()
{
	int num;
	int wait_ms = -1;
	SEpollEvent events[4096], *pv;
	CTcpSession *session;
	uint32_t now = osl_get_ms();
	STcpLink link;
	SPacketHeader packet_header;
	char_t buf[65536];
	int size;
	void *position = NULL;
	bool dead_flag = false;
	void *nextpos = NULL;
	STimeout timeout;
	int ret=0;

	now = osl_get_ms();

	if(m_lastTick == 0)
		m_lastTick = now;

	//���wait_ms ��ʱ��
	position = m_timeout.GetFirst(&timeout);
	if(position)
		wait_ms = (int32_t)((int64_t)timeout.currentMS - (int64_t)now);

	//wait_ms �����ܴ���60�룬������С��0(-1����)
	if(wait_ms > 60000)
		wait_ms = 60000;

	if(now < m_lastTick || (wait_ms != -1 && wait_ms < 0))//ʱ�����
		wait_ms = 0;
	num = osl_epoll_wait( m_epoll, events, sizeof(events)/sizeof(events[0]), wait_ms );
	for( int i=0; i<num; i++ )
	{
		pv = events + i;

		if (pv->events & (OSL_EPOLL_HUP | OSL_EPOLL_ERR))//�����ˣ������ɾ��
		{
			if(pv->data.fd != m_group_accpet_socket && pv->data.fd != m_notify_fd)
			{
				position = pv->data.ptr;
				session = *m_sessions.GetValue( position ); 
				if( session!=NULL && session->m_link.skt != -1 )
				{
					//�����ݽ����ˣ���һ�ε�timer��ɾ��
					osl_log_debug("[%s] EPOLL_ERR or EPOLL_HUP\n",__func__);
					CancelTimer(session->GetTimerId());
					KillLink(position);
				}
			}
			
		}
		else if( pv->events & OSL_EPOLL_IN)//������Ҫ����
		{
			if(m_group_accpet_socket == pv->data.fd)//�µ�socket
			{
				ReadSocket(m_group_accpet_socket);

				//����������
				while(m_newQueue.Read(&link))
				{
					position = ActivateLink( link );
					if (position == NULL)
					{
						osl_log_error( "[CTcpGroup][OnWork] warn: session NULL\n" );
						osl_socket_destroy( link.skt );
					}
				}
			}
			else if (m_notify_fd == pv->data.fd)//���Ͷ���������Ҫ����
			{
				ReadSocket(m_notify_fd);		
				while(true)
				{
					size = m_outerCache.Read(&packet_header, buf, sizeof(buf));
					if (size <= 0)
						break;
					position = m_sessions.Search(&packet_header.link.skt_idx, &session);
					if (position)
					{
						session->HandleBeforeSend(packet_header,buf,size);
						dead_flag = session->OnSend(buf, size, now);
						if(dead_flag)
						{
							osl_log_debug("[%s] skt_idx:%llu socket:%d send error close it\n",__func__,session->m_link.skt_idx,session->m_link.skt);
							KillLink(position);
						}
						else
						{
							if(0 < session->m_send_datsize)//û���꣬�´ν��ŷ�
							{
								osl_log_debug("===send_size :%d position:%x skt:%d\n",session->m_send_datsize,position,session->m_link.skt);
								SetEvent(session->m_link.skt,OSL_EPOLL_CTL_MOD,OSL_EPOLL_OUT,position,0);
							}
							else
							{
								//������,kill 0x4 ��ʾ�����һ��������Ϊһ�������İ����ֶܷ��������
								if((packet_header.flag&PACKET_END )&&session->GetCloseFlag())
								{
									osl_log_debug("[%s] skt_idx:%llu socket:%d short connection later kill  it\n",__func__,session->m_link.skt_idx,session->m_link.skt);
									CancelTimer(session->GetTimerId());
									//KillLink(position);
									//�����ÿͻ��������رգ����������رվ��ӳ�1���ɷ������ص�
									session->SetTimerId(SetTimer(session->m_link.skt_idx,position,1000));//����������timer
								}
							}
						}
					}
				}//while(true)
			}
			else
			{
				position = pv->data.ptr;
				session = *m_sessions.GetValue( position );
				if( session!=NULL && session->m_link.skt != -1 && session->HandShake())
				{
					//�����ݽ����ˣ���һ�ε�timer��ɾ��
					CancelTimer(session->GetTimerId());
					dead_flag = session->OnRecv(now);
					if(dead_flag)
					{
						osl_log_debug("[%s] skt_idx:%llu socket:%d close by client\n",__func__,session->m_link.skt_idx,session->m_link.skt);
						KillLink(position);
					}
					else
					{
						//�������ó�ʱ
						session->SetTimerId(SetTimer(session->m_link.skt_idx,position));//����������timer
					}
				}
			}
		}
		else if( pv->events & OSL_EPOLL_OUT )//������Ҫ����
		{
			position = pv->data.ptr;
			session = *m_sessions.GetValue( position );
			if( session!=NULL && session->m_link.skt != -1 && session->HandShake())
			{
				osl_log_debug("epoll out position:%x skt:%d\n",position,session->m_link.skt);
				dead_flag = session->OnSend(NULL, 0, now);
				if(dead_flag)
				{
					osl_log_debug("[%s] skt_idx:%llu socket:%d send error close it\n",__func__,session->m_link.skt_idx,session->m_link.skt);
					KillLink(position);
				}
				else
				{
					if (session->m_send_datsize <= 0)//������ɣ��Ӷ�����ɾ��SESSION
					{
						if(session->GetCloseFlag())//������,kill
						{
							osl_log_debug("short connection later will kill it\n");
							CancelTimer(session->GetTimerId());
							//KillLink(position);
							//�����ÿͻ��������رգ����������رվ��ӳ�1���ɷ������ص�
							session->SetTimerId(SetTimer(session->m_link.skt_idx,position,1000));//����������timer
						}
						SetEvent(session->m_link.skt,OSL_EPOLL_CTL_MOD,OSL_EPOLL_IN,position,0);
					}
					else
					{
						//û���꣬�ͽ��ŷ�
						SetEvent(session->m_link.skt,OSL_EPOLL_CTL_MOD,OSL_EPOLL_OUT,position,0);
					}
				}
			}
		}
		
		
	}

	if(now < m_lastTick)//osl_get_ms ʱ�������
	{
		osl_log_debug("func:%s time backstrace\n",__func__);
		m_timeout.RemoveAll();
		m_timers.RemoveAll();
		//ʱ������ˣ���ȫ��������һ��
		position = m_sessions.GetFirst( NULL );
		while( position )
		{
			nextpos = m_sessions.GetNext(NULL, position);
			session = *m_sessions.GetValue(position);
			session->SetTimerId(SetTimer(session->m_link.skt_idx,position));//����������timer
			position = nextpos;
		}	
	}
	
	HandleTimeout();//����ʱ

	m_lastTick = now;

	return 0;
}


//��fd�����ݶ���
void CTcpGroup::ReadSocket(int32_t fd)
{
	//ret = read ( m_group_accpet_socket, (char *)data, sizeof(data) );
	int32_t ret;
	char_t data[1024];
	
	//if ( (0 > ret) && (errno != EAGAIN) )
	//{
	//	osl_log_debug ( "NOTIFY: read data failed fd %i size %i errno %i '%s'", m_group_accpet_socket, ret, errno, strerror(errno) );
	//}

	while(1)
	{
		ret = read ( fd, (char *)data, sizeof(data) );
		if(ret <= 0)
			break;
	}
}

//��ȡ����ʽ
int32_t CTcpGroup::GetModel()
{
	return m_model;
}

//�����̴߳�����
int32_t CTcpGroup::WorkProc( void* param, void* expend )
{
	return ((CTcpGroup *)param)->OnWork();
}

//��ռʽ������
int32_t CTcpGroup::OnWork()
{
	/*�̴߳���ģ��һ��������,һ������ռʽ��һ���Ƿ���ʽ����ռʽ���ܱ�֤��Ϣ��˳���ԣ�����ʽȡģ���Ա�֤��Ϣ��˳���ԣ�ͨ���Է�

����ȡģ�������Ƿ���ʽҪ���䲻���ȵĻ������ܻᵼ����Щ�̺߳�æ����Щȴ���У���ռʽ�Ϳ��Գ�ֵ�����cpu,������ͨ��http���󣬾Ϳ�������ռʽ*/

	int ret = 1;	
	uint32_t now = osl_get_ms();
	SPacket packet;
	//SPacketHeader packet_header;
	int32_t size; 
	char_t buf[65536];

	osl_mutex_lock(m_inner_cache_mutex,-1);
	size = m_innerCache.Read(&packet, buf, sizeof(buf));
	
	if (size <= 0)
	{		
		//��pthread_cond_signal ����ʱ����wait����������
		pthread_cond_wait( m_inner_cache_cond, (pthread_mutex_t*)m_inner_cache_mutex);	

		size = m_innerCache.Read(&packet, buf, sizeof(buf));
		if ( size <= 0)
		{
			osl_mutex_unlock( m_inner_cache_mutex );
			goto RET;
		}
	}
	osl_mutex_unlock( m_inner_cache_mutex );
		
	if(!m_innerCache.IsEmpty())
	{
		pthread_cond_signal( m_inner_cache_cond );
		//osl_log_debug("signal\n");
	}
	
	if(size != packet.len)
	{
		osl_log_debug("[%s] maybe something error size:%d packet.len:%d \n",__func__,size,packet.len);
		goto RET;
	}

	if(size < sizeof(buf))
		buf[size] = 0;
	packet.buf = buf;
	if(packet.header.mode == PACKET_MODE_HTTP)
		g_service_disp.OnHttpPacekt( GetServer(), this, &packet.header, packet.buf, packet.hlen, packet.clen, now );
	
RET:
	return 0;
}

//�����̴߳�����
int32_t CTcpGroup::WorkProc1( void* param, void* expend )
{
	return ((CTcpGroup *)param)->OnWork1(expend);
}

//����ʽ������
int32_t CTcpGroup::OnWork1(void *expend)
{
	SPacket packet;
	SThreadArg *arg = (SThreadArg*)expend;
	int32_t cnt = 0;
	uint32_t now = osl_get_ms();
	char_t buf[65536];
	int32_t size;
	
	while(arg)
	{
		size = arg->m_innerCache.Read(&packet,buf,sizeof(buf));
		if(size <= 0)
			break;
		cnt++;

		if(packet.len != size)
		{
			osl_log_error("maybe something error size:%d packet.len\n",size,packet.len);
			continue;
		}

		if(size < sizeof(buf))
			buf[size] = 0;
		packet.buf = buf;
		//osl_log_debug("get buffer hlen:%d clen:%d packet.buf:%x\n",packet.hlen,packet.clen,packet.buf);
		if(packet.header.mode == PACKET_MODE_HTTP)
			g_service_disp.OnHttpPacekt( GetServer(), this, &packet.header, packet.buf, packet.hlen, packet.clen, now );

		if(cnt > 100)
		{
			//���ÿ�δ���100������ֹcpu�ﵽ100%
			goto RET;
		}
	}
	
RET:
	return 1;
}

void CTcpGroup::HandleTimeout()
{
	uint32_t time_now = osl_get_ms();
	STimeout timeout;
	STimer timer;
	void *position;
	void *nextpos;
	void *tmp_pos;
	void *session_pos;
	CTcpSession *session;
	
	position = m_timeout.GetFirst(&timeout);
	while(position)
	{
		nextpos = m_timeout.GetNext(NULL, position);
		timeout = *m_timeout.GetValue(position);
		if(timeout.currentMS < time_now)
		{
			//��ʱ�ˣ��ɵ�
			//�ҵ�timeid;
			tmp_pos = m_timers.Search(&timeout.timerID,&timer);
			if(tmp_pos)
			{
				session = *m_sessions.GetValue( timer.position );
				if(session!=NULL && session->m_link.skt != -1)
				{
					KillLink( timer.position);
					osl_log_debug("socket idx:%lld timeout kill it\n",timer.skt_idx);
				}

				//ɾ��timerID 
				m_timers.RemoveByPosition(tmp_pos);
			}

			m_timeout.RemoveByPosition(position);
			position = nextpos;
		}
		else//����currentMS��С���������
			break;
	}
}


//����������
void* CTcpGroup::ActivateLink( STcpLink link )
{
	CTcpServer *server = (CTcpServer*)m_server;
	void *position;
	CTcpSession *session;
	int32_t num;

	//position = m_sessions.Search( &link.skt, &session );
	//if( position )//�����ظ�skt��ɾ���ɵ�
	//{
	//	session->Stop();
	//	m_sessions.RemoveByPosition( position );
	//}
	//else
	num = m_dumps.GetSize();
	if( 0 < num )//Ѱ��һ���������ӻ�������
	{
		session = m_dumps.GetAt( num-1 );
		m_dumps.RemoveAt( num-1 );
	}
	else//�޿���session���½�
	{
		if (server->m_proc)
		{
			session = server->m_proc(server->m_param);
		}
		else
		{
			session = new CTcpSession();
		}
	}

	session->Start(this, link);
	position = m_sessions.Insert(session);
	if(position)
	{
		session->SetTimerId( SetTimer(link.skt_idx,position));
		SetEvent(link.skt,OSL_EPOLL_CTL_ADD,OSL_EPOLL_IN,position,0);
	}

	return position;
}

//ɱ�������ӣ�δ�����ڲ�����
void CTcpGroup::KillLink( void *position )
{
	CTcpSession *session;

	session = *m_sessions.GetValue( position );
	SetEvent(session->m_link.skt,OSL_EPOLL_CTL_DEL,OSL_EPOLL_IN,position,0);
	session->Stop();
	m_sessions.RemoveByPosition(position);

	m_dumps.Add(session);
}

uint32_t CTcpGroup::SetTimer(uint64_t idx,void *position,int32_t timems)
{
	//���볬ʱtask����
	STimer timer;
	
	timer.timerID  = m_timerID++;
	timer.skt_idx = idx;
	timer.position = position;
	//task.session_pos = session_pos;

	STimeout timeout;
	timeout.currentMS = osl_get_ms() + timems;//60�볬ʱ
	timeout.timerID = timer.timerID;
	
	m_timers.Insert(timer);
	m_timeout.Insert(timeout);

	//osl_log_debug("add timer %d skt_idx:%llu time:%u\n",timer.timerID,timer.skt_idx,timeout.currentMS);
	return timer.timerID;
}

void CTcpGroup::CancelTimer(uint32_t timerid)
{
	//uint32_t tmp_timerid = timerid
	m_timers.Remove(&timerid);
	//osl_log_debug("caceltime :%u \n",timerid);
}


//m_sessions����ȽϺ���
int32_t CTcpGroup::CompareSessionCallback(bool item1_is_key, void* item1, void* item2, void *param )
{
	int64_t  idx1, idx2;
	if( item1_is_key )
	{
		idx1 = *(int64_t*)item1;
		idx2 = (*(CTcpSession**)item2)->m_link.skt_idx;
	}
	else
	{
		idx1 = (*(CTcpSession**)item1)->m_link.skt_idx;
		idx2 = (*(CTcpSession**)item2)->m_link.skt_idx;
	}

	if( idx1 < idx2 )
		return -1;
	else if( idx1 == idx2 )
		return 0;
	else
		return 1;
}

int32_t CTcpGroup::CompareTaskByTimerIDCallback(bool item1_is_key, void* item1, void* item2, void *param )
{
	uint32_t timerid1,timerid2;
	if(item1_is_key)
	{
		timerid1 = *(uint32_t*)item1;
		timerid2 = ((STimer*)item2)->timerID;
		if (timerid1 < timerid2)
			return -1;
		else if(timerid1 > timerid2)
			return  1;
		else 
			return 0;
	}
	else 
	{
		timerid1 = ((STimer*)item1)->timerID;
		timerid2 = ((STimer*)item2)->timerID;
		if (timerid1 < timerid2)
			return -1;
		else if(timerid1 > timerid2)
			return  1;
		else 
			return 0;
	}
}


int32_t CTcpGroup::CompareTaskByTimeMsCallback(bool item1_is_key, void* item1, void* item2, void *param )
{
	uint64_t currentms1,currentms2;
	if(item1_is_key)
	{
		currentms1 = *(uint64_t*)item1;
		currentms2 = ((STimeout*)item2)->currentMS;
		if (currentms1 < currentms2)
			return -1;
		else if(currentms1 > currentms2)
			return  1;
		else 
			return 0;
	}
	else 
	{
		currentms1 = ((STimeout*)item1)->currentMS;
		currentms2 = ((STimeout*)item2)->currentMS;
		if (currentms1 < currentms2)
			return -1;
		else if(currentms1 > currentms2)
			return  1;
		else 
			return 0;
	}
}

