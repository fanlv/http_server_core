#ifndef __TCPCACHE_CPP__
#define __TCPCACHE_CPP__

#include "stdafx.h"
#include "osl.h"
#include "osl_log.h"
#include "osl_rwlock.h"
#include "osl_string.h"
#include "TcpCache.h"

template<class TYPE>
CTcpCache<TYPE>::CTcpCache()
{
	m_buf = NULL;
	m_bufsize = 0;
	m_head = 0;
	m_tail = 0;
}

template<class TYPE>
CTcpCache<TYPE>::~CTcpCache()
{
	Destroy();
}


//初始化
template<class TYPE>
void CTcpCache<TYPE>::Create( int32_t bufsize )
{
	m_bufsize = bufsize;
	m_buf = (uchar_t*)malloc(bufsize);
	m_head = 0;
	m_tail = 0;
}

//释放
template<class TYPE>
void CTcpCache<TYPE>::Destroy()
{
	if (m_buf)
	{
		free(m_buf);
		m_buf = NULL;
	}
}

//邮递消息
template<class TYPE>
bool CTcpCache<TYPE>::Post( TYPE& packet_header, char_t *buf, int32_t size )
{
	int32_t h, t;
	int32_t datsize, cpysize;
	uchar_t tmp[sizeof(TYPE)+4];
	int32_t tmp_size;
	int32_t real_size;
	bool ret = false;

	//osl_log_debug("funs:%s  size:%d h:%d t:%d bufsize:%d \n",__func__,size,m_head,m_tail,m_bufsize);
	
	real_size = size;  // 保存原始size, 代码过程当中修改了这个值
	tmp_size = sizeof(TYPE)+4;
	h = m_head;
	t = m_tail;
	
	datsize = (t - h + m_bufsize) % m_bufsize;
	if (datsize + tmp_size + size < m_bufsize)
	{
		//link(x)+size(4)+buf(size) + link(x)+size(4)+buf(size) + ...
		
		memcpy(tmp, &packet_header, sizeof(TYPE));
		memcpy(tmp+sizeof(TYPE), &size, 4);

		//---h----t---
		if (h <= t)
		{
			cpysize = m_bufsize - t;
			if (tmp_size <= cpysize)
			{
				memcpy(m_buf+t, tmp, tmp_size);
			}
			else
			{
				memcpy(m_buf+t, tmp, cpysize);
				tmp_size -= cpysize;
				memcpy(m_buf, tmp+cpysize, tmp_size);
			}
		}
		//---t----h----
		else
		{
			memcpy(m_buf+t, tmp, tmp_size);
		}

		tmp_size = sizeof(TYPE)+4;  // 这里重新赋值是因为上面代码有可能改动了这个值
		t = (t + tmp_size) % m_bufsize;


		if(size > 0 && buf)
		{
			//---h----t---
			if (h <= t)
			{
				cpysize = m_bufsize - t;
				if (size < cpysize)
				{
					memcpy(m_buf+t, buf, size);
				}
				else
				{
					memcpy(m_buf+t, buf, cpysize);
					size -= cpysize;
					memcpy(m_buf, buf+cpysize, size);
				}
			}
			//---t----h----
			else
			{
				memcpy(m_buf+t, buf, size);
			}
		}

		m_tail = (t + real_size) % m_bufsize;

		ret = true;
	}

	//osl_log_debug("head %d tail:%d\n",m_head,m_tail);
	return ret;
}

//读取消息，一次一条
template<class TYPE>
int32_t CTcpCache<TYPE>::Read( TYPE *link, char_t *buf, int32_t size )
{
	int32_t pktsize, cpysize, datsize;
	int32_t real_pktsize;
	int32_t h, t, i;
	uchar_t *p;

	//osl_log_debug("read head:%d tail:%d\n",m_head,m_tail);

	h = m_head;
	t = m_tail;
	datsize = (t - h + m_bufsize) % m_bufsize;

//	osl_log_debug("read sizeof(TYPE):%d datsize:%d\n",sizeof(TYPE),datsize);
	if( sizeof(TYPE) + 4 <= datsize )
	{
		//读取link
		p = (uchar_t*)link;
		for (i=0; i<sizeof(TYPE); i++)
			p[i] = m_buf[(h+i)%m_bufsize];
		h = (h + sizeof(TYPE)) % m_bufsize;

		//读取size
		p = (uchar_t*)&pktsize;
		for (i=0; i<4; i++)
			p[i] = m_buf[(h+i)%m_bufsize];
		real_pktsize = pktsize; // 保存解析出来的记录长度
		h = (h + 4) % m_bufsize;

		if(pktsize > size)
		{
			osl_log_error("real_pktsize:%d size:%d size too small\n",real_pktsize,size);
			pktsize = size;
		}

		if (sizeof(TYPE) + 4 + pktsize <= datsize)
		{
			if(size > 0)
			{
				//---h----t---
				if (h < t)
				{
					memcpy(buf, m_buf+h, pktsize);
				}
				//---t----h----
				else
				{
					cpysize = m_bufsize - h;
					if (pktsize < cpysize)
					{
						memcpy(buf, m_buf+h, pktsize);
					}
					else
					{
						memcpy(buf, m_buf+h, cpysize);
						pktsize -= cpysize;
						memcpy(buf, m_buf, pktsize);
					}
				}
			}

			m_head = (m_head + real_pktsize + sizeof(TYPE) + 4) % m_bufsize;
			//osl_log_debug("read head:%d tail:%d pktsize:%d real_pktsize:%d\n",m_head,m_tail,pktsize,real_pktsize);
			return real_pktsize;
		}
		else//出错
		{
			//osl_log_warn("[CTcpCache][Read] tcp cache error !!! \n");
			m_head = m_tail;
		}
	}

	return 0;
}

//读取消息的头部,但是不移动任何下标
template<class TYPE>
int32_t CTcpCache<TYPE>::ReadHeader( TYPE *link)
{
	int32_t datsize,real_pktsize = 0;
	int32_t h, t, i;
	uchar_t *p;

	h = m_head;
	t = m_tail;
	datsize = (t - h + m_bufsize) % m_bufsize;
	if( sizeof(TYPE) + 4 < datsize )
	{
		//读取link
		p = (uchar_t*)link;
		for (i=0; i<sizeof(TYPE); i++)
			p[i] = m_buf[(h+i)%m_bufsize];
		h = (h + sizeof(TYPE)) % m_bufsize;

		//读取size
		p = (uchar_t*)&real_pktsize;
		for (i=0; i<4; i++)
			p[i] = m_buf[(h+i)%m_bufsize];

		if(real_pktsize > 65536)//一次不可能大于64k,若大于,一定出错了
		{
			osl_log_error("[%s] real_pktsize:%d too large\n",__func__,real_pktsize);
			real_pktsize = 65536;
		}

		if(real_pktsize < 0)
		{
			osl_log_error("[%s] real_pktsize:%d too small\n",__func__,real_pktsize);
			real_pktsize = 0;
		}

		return real_pktsize;
	}

	return 0;
}


//清除
template<class TYPE>
void CTcpCache<TYPE>::Clear()
{
	m_head = 0;
	m_tail = 0;
}

template<class TYPE>
bool CTcpCache<TYPE>::IsEmpty()
{
	return m_tail == m_head ? true : false;
}
#endif
