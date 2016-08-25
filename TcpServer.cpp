#include "stdafx.h"
#include "osl_epoll.h"
#include "osl_socket.h"
#include "osl_log.h"
#include "osl_thread.h"
#include "osl_mutex.h"
#include "Xtc.h"
#include "XtcArray.h"
#include "TcpServer.h"
#include "TcpGroup.h"
#ifdef _OPENSSL
#include "openssl/ssl.h"
#endif
#include "main.h"


//TCP�Ự������
CTcpServer::CTcpServer()
{
#ifdef _OPENSSL
	m_ssl_ctx = NULL;
#endif

	m_listen_skt = -1;
	m_listen_ip = 0;
	m_listen_port = 0;
	m_ssl_flag = false;
	memset(&m_statistics, 0, sizeof(m_statistics));

	m_thread = NULL;
	m_proc = NULL;
	m_param = NULL;
	m_skt_idx = 0;
}


CTcpServer::~CTcpServer()
{
	Release();
}


/*��������,timeout=-1��ʾ����ⳬʱ thread_num����0�Ļ����ա���������ȫ��dispatch������ɣ��Ա�֤��Ϣ�����ԣ������Ҫ����
	need_response ��ʾһ�ʾ�Ҫһ��model==1 ����ռʽ����(����֤ʱ����)��model==2,����ʽ����(��֤ʱ����)
*/

bool CTcpServer::Initialize( uint32_t listen_ip, uint16_t listen_port, bool ssl_flag,int group_num,
		int32_t thread_num,int32_t model,bool need_response, PTcpNewCallback proc, void *param)
{
	int32_t i, cpu_num;
	CTcpGroup *grp = NULL;
	uint32_t unblock = 1;
	int32_t flag = 1;
	int32_t size = 65536*5;
	SOCKET skt;
	int32_t maxnum = 65536;


#ifdef _OPENSSL
	m_ssl_ctx = NULL;
#endif
	m_listen_skt = -1;
	m_listen_ip = 0;
	m_listen_port = 0;
	m_ssl_flag = false;
	memset(&m_statistics, 0, sizeof(m_statistics));
	m_thread = NULL;
	m_proc = NULL;
	m_param = NULL;

	//����Socket, ʹ��TCPЭ��
	skt = osl_socket_create( AF_INET, SOCK_STREAM, 0 );
	if( skt == -1 )
	{
		osl_log_error("[CTcpServer][Initialize] create socket error!\n");
		goto ERROR_EXIT;
	}

	//��������ʽ
	osl_socket_ioctl( skt, FIONBIO, &unblock );

#ifdef _OPENSSL
	if( ssl_flag )
	{
		m_ssl_ctx = SSL_CTX_new( SSLv23_server_method() );
		if( m_ssl_ctx == NULL )
		{
			osl_log_error("[CTcpServer][Initialize] create SSL_CTX_new error !!!\n");
			goto ERROR_EXIT;
		}
			
		
		SSL_CTX_load_verify_locations( m_ssl_ctx, "./config", NULL );
		SSL_CTX_set_verify_depth( m_ssl_ctx, 1 );

		SSL_CTX_set_verify( m_ssl_ctx, SSL_VERIFY_NONE, NULL );

		//֤���ļ�������ڱ���֤���ļ�
		SSL_CTX_set_default_passwd_cb_userdata( m_ssl_ctx, (void*)"pass phrase" );
		//SSL_CTX_set_default_passwd_cb_userdata( m_ssl_ctx, (void*)"123456" );
		//��ȡ֤���ļ�
		if( SSL_CTX_use_certificate_chain_file( m_ssl_ctx, "./config/certificate.crt" ) <= 0 )
		//if( SSL_CTX_use_certificate_chain_file( m_ssl_ctx, "./config/server.crt" ) <= 0 )
		{
			osl_log_error("[CTcpServer][Initialize] crt error\n");
			goto ERROR_EXIT;
		}
		//��ȡ��Կ�ļ�
		if( SSL_CTX_use_PrivateKey_file(m_ssl_ctx,"./config/certificate.crt",SSL_FILETYPE_PEM) <= 0 )
		//if( SSL_CTX_use_PrivateKey_file(m_ssl_ctx,"./config/server.key",SSL_FILETYPE_PEM) <= 0 )
		{
			osl_log_error("[CTcpServer][Initialize] privkey key error\n");
			goto ERROR_EXIT;
		}
		//��֤��Կ�Ƿ���֤��һ��
		if( !SSL_CTX_check_private_key( m_ssl_ctx) )
		{
			osl_log_error("[CTcpServer][Initialize] key error\n");
			goto ERROR_EXIT;
		}
//		SSL_CTX_set_mode( m_ssl_ctx, SSL_MODE_AUTO_RETRY);
	}
#endif

	//����ϴ������쳣�˳����˿ڿ��ܳ���TIME_WAIT״̬���޷�ֱ��ʹ�ã�����SO_REUSEADDR�ɱ�֤bind()�ɹ�
	osl_socket_set_opt( skt, SOL_SOCKET, SO_REUSEADDR, (char*)&flag, sizeof(flag) );
	osl_socket_set_opt( skt, SOL_SOCKET, SO_SNDBUF, (char *)&size, sizeof(size) );
	osl_socket_set_opt( skt, SOL_SOCKET, SO_RCVBUF, (char *)&size, sizeof(size) );

	//�󶨽��յ�ַ�Ͷ˿�
	if( osl_socket_bind( skt, listen_ip, listen_port ) != 0 )
	{
		osl_log_error("[CTcpServer][Initialize] bind error !!!\n");
		goto ERROR_EXIT;
	}

	//if( osl_socket_listen( skt, SOMAXCONN ) != 0 )
	//SOMAXCONN linux ��Ĭ�ϵ���128  ̫С������Ĵ��
	//�ο� http://blog.csdn.net/taodirecte/article/details/6800979
	
	//��������
	if( osl_socket_listen( skt, 8192 ) != 0 )
	{
		osl_log_error("[CTcpServer][Initialize] listen error !!!\n");
		goto ERROR_EXIT;
	}

	m_listen_skt = skt;
	m_listen_ip = listen_ip;
	m_listen_port = listen_port;
	m_ssl_flag = ssl_flag;
	m_proc = proc;
	m_param = param;

	cpu_num = osl_get_cpu_count();
	if(cpu_num <= 0 )
		cpu_num = 4;
	if (thread_num == 0 )
		thread_num = cpu_num;// * 3;
	if(group_num == 0 )
		group_num = 4;


	osl_log_debug("=-====================group_num:%d cpu_num:%d thread_num:%d\n",group_num,cpu_num,thread_num);
	m_groups.SetSize(group_num, cpu_num);
	for(i=0; i<group_num; i++)
	{
		grp = new CTcpGroup();
		if (grp != NULL)
		{
			grp->Start( this, i%cpu_num,thread_num,cpu_num ,model,need_response);
			m_groups[i] = grp;
		}
		else
		{
			osl_log_error("[CTcpServer][Initialize] new CTcpGroup error !!!\n");
		}
		
	}

	m_thread = osl_thread_create( "tTcpServerListen", 200, 4*1024*1024, CTcpServer::ListenProc, this, NULL );
	if ( m_thread )
	{
		osl_thread_bind_cpu( m_thread, 0 );
		osl_thread_resume( m_thread );
	}
	else
	{
		osl_log_error("[CTcpServer][Initialize] osl_thread_create error !!!\n");
	}

	osl_log_error("[CTcpServer][Initialize] success !!!\n");
	return true;

ERROR_EXIT:
	osl_log_error("[CTcpServer][Initialize] error !!!\n");
	Release();
	return false;
}

//ֹͣ����
void CTcpServer::Release()
{
	CTcpGroup *grp;
	int32_t i;

	if( m_thread )
	{
		osl_thread_destroy( m_thread, -1 );
		m_thread = NULL;
	}
	for(i=0; i<m_groups.GetSize(); i++)
	{
		grp = m_groups[i];
		grp->Stop();
		delete grp;
	}
	m_groups.RemoveAll();

	if( m_listen_skt != -1 )
	{
		osl_socket_destroy( m_listen_skt );
		m_listen_skt = -1;
	}

#ifdef _OPENSSL
	if( m_ssl_ctx )
	{
		SSL_CTX_free( m_ssl_ctx );
		m_ssl_ctx = NULL;
	}
#endif
}

//�Ƿ��Ѿ���ʼ��
bool CTcpServer::IsInitialized()
{
	return m_thread != NULL;
}

//�������ݣ����뻺���������أ��Ѽ����̰߳�ȫ
bool CTcpServer::PostData( STcpLink link, char_t *buf, int32_t size )
{
	int32_t i;
	CTcpGroup *grp;
	SPacketHeader header;

	memset(&header,0,sizeof(header));
	header.link = link;
	header.flag = PACKET_START;

	//Ѱ�Ҹ�skt���ĸ�group�У��ɷ���ȥ
	//for (i=0; i<m_groups.GetSize(); i++)
	//{
	//	grp = m_groups[i];
	//	if (grp->PostData( header, buf, size ))
	//		return true;
	//}

	grp = m_groups[link.skt_idx % m_groups.GetSize()];
	grp->PostData( header, buf, size );
	return true;
}

//ȡ��ͳ����Ϣ
void CTcpServer::GetStatistics( STcpServerStatistics *sta )
{
	int32_t i;

	*sta = m_statistics;
	sta->session_cnt = 0;
	for (i=0; i<m_groups.GetSize(); i++)
	{
		sta->session_cnt += (m_groups[i]->m_sessions).GetSize();
	}
}

//���ͳ����Ϣ
void CTcpServer::AddStatistics( STcpServerStatistics& sta )
{
	m_statistics.send_size	+= sta.send_size;
	m_statistics.recv_size	+= sta.recv_size;
	m_statistics.cmd_cnt	+= sta.cmd_cnt;
	m_statistics.connect_cnt+= sta.connect_cnt;
}

//����������
int32_t CTcpServer::ListenProc( void* param, void* expend )
{
	return ((CTcpServer*)param)->OnListen();
}

int32_t CTcpServer::OnListen()
{
	int32_t ret;
	SOCKET skt;
	uint32_t remote_ip;
	uint16_t remote_port;
	CTcpGroup *grp = NULL;
	int32_t i, n;
	uint32_t now;
	STcpLink link;

	now = osl_get_ms();

	//�ȴ�������
	for( i=0; i<1000; i++ )
	{
		skt = osl_socket_accept( m_listen_skt, &remote_ip, &remote_port );
		if( skt == -1 )
			break;
//		osl_log_debug(">>>>>>>>>>>>accept=skt=%d\n",skt);

		m_statistics.connect_cnt++;
		m_statistics.session_cnt++;

		memset(&link, 0, sizeof(link));
		link.skt = skt;
		link.remote_ip = remote_ip;
		link.remote_port = remote_port;
		link.skt_idx = m_skt_idx;
		grp = m_groups[m_skt_idx%m_groups.GetSize()];
		grp->SetLinkBorn(link);
		m_skt_idx ++ ;
		ret = 1;
	}
	return 1;
}
