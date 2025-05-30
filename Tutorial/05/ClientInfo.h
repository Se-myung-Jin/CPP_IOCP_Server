#pragma once

#include "Define.h"
#include <stdio.h>
#include <mutex>

//클라이언트 정보를 담기위한 구조체
class stClientInfo
{
public:
	stClientInfo()
	{
		ZeroMemory(&mRecvOverlappedEx, sizeof(stOverlappedEx));
		ZeroMemory(&mSendOverlappedEx, sizeof(stOverlappedEx));
		mSock = INVALID_SOCKET;
	}

	~stClientInfo() = default;


	void Init(const UINT32 index)
	{
		mIndex = index;
	}

	UINT32 GetIndex() { return mIndex; }

	bool IsConnectd() { return mSock != INVALID_SOCKET; }
	
	SOCKET GetSock() { return mSock; }

	char* RecvBuffer() { return mRecvBuf; }


	bool OnConnect(HANDLE iocpHandle_, SOCKET socket_)
	{
		mSock = socket_;

		Clear();

		//I/O Completion Port객체와 소켓을 연결시킨다.
		if (BindIOCompletionPort(iocpHandle_) == false)
		{
			return false;
		}

		return BindRecv();
	}

	void Close(bool bIsForce = false)
	{
		struct linger stLinger = { 0, 0 };	// SO_DONTLINGER로 설정

	// bIsForce가 true이면 SO_LINGER, timeout = 0으로 설정하여 강제 종료 시킨다. 주의 : 데이터 손실이 있을수 있음 
		if (true == bIsForce)
		{
			stLinger.l_onoff = 1;
		}

		//socketClose소켓의 데이터 송수신을 모두 중단 시킨다.
		shutdown(mSock, SD_BOTH);

		//소켓 옵션을 설정한다.
		setsockopt(mSock, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));

		//소켓 연결을 종료 시킨다. 
		closesocket(mSock);		
		mSock = INVALID_SOCKET;
	}

	void Clear()
	{		
		mSendPos = 0;
		mIsSending = false;
	}

	bool BindIOCompletionPort(HANDLE iocpHandle_)
	{
		//socket과 pClientInfo를 CompletionPort객체와 연결시킨다.
		auto hIOCP = CreateIoCompletionPort((HANDLE)GetSock()
			, iocpHandle_
			, (ULONG_PTR)(this), 0);

		if (hIOCP == INVALID_HANDLE_VALUE)
		{
			printf("[에러] CreateIoCompletionPort()함수 실패: %d\n", GetLastError());
			return false;
		}

		return true;
	}

	bool BindRecv()
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		//Overlapped I/O을 위해 각 정보를 셋팅해 준다.
		mRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		mRecvOverlappedEx.m_wsaBuf.buf = mRecvBuf;
		mRecvOverlappedEx.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(mSock,
			&(mRecvOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			&dwFlag,
			(LPWSAOVERLAPPED) & (mRecvOverlappedEx),
			NULL);

		//socket_error이면 client socket이 끊어진걸로 처리한다.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[에러] WSARecv()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		return true;
	}

	// 1개의 스레드에서만 호출해야 한다!
	bool SendMsg(const UINT32 dataSize_, char* pMsg_)
	{
		std::lock_guard<std::mutex> guard(mSendLock);

		if ((mSendPos + dataSize_) > MAX_SOCK_SENDBUF)
		{
			mSendPos = 0;
		}

		auto pSendBuf = &mSendBuf[mSendPos];
		
		//전송될 메세지를 복사
		CopyMemory(pSendBuf, pMsg_, dataSize_);
		mSendPos += dataSize_;

		return true;
	}

	bool SendIO()
	{
		if (mSendPos <= 0 || mIsSending)
		{
			return true;
		}

		std::lock_guard<std::mutex> guard(mSendLock);

		mIsSending = true;

		CopyMemory(mSendingBuf, &mSendBuf[0], mSendPos);
		
		//Overlapped I/O을 위해 각 정보를 셋팅해 준다.
		mSendOverlappedEx.m_wsaBuf.len = mSendPos;
		mSendOverlappedEx.m_wsaBuf.buf = &mSendingBuf[0];
		mSendOverlappedEx.m_eOperation = IOOperation::SEND;

		DWORD dwRecvNumBytes = 0;
		int nRet = WSASend(mSock,
			&(mSendOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			0,
			(LPWSAOVERLAPPED) & (mSendOverlappedEx),
			NULL);

		//socket_error이면 client socket이 끊어진걸로 처리한다.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[에러] WSASend()함수 실패 : %d\n", WSAGetLastError());
			return false;
		}

		mSendPos = 0;
		return true;
	}

	void SendCompleted(const UINT32 dataSize_)
	{
		mIsSending = false;
		printf("[송신 완료] bytes : %d\n", dataSize_);
	}


private:
	INT32 mIndex = 0;
	SOCKET			mSock;			//Cliet와 연결되는 소켓
	stOverlappedEx	mRecvOverlappedEx;	//RECV Overlapped I/O작업을 위한 변수
	stOverlappedEx	mSendOverlappedEx;	//SEND Overlapped I/O작업을 위한 변수

	char			mRecvBuf[MAX_SOCKBUF]; //데이터 버퍼

	std::mutex mSendLock;
	bool mIsSending = false;
	UINT64 mSendPos = 0;
	char			mSendBuf[MAX_SOCK_SENDBUF]; //데이터 버퍼	
	char			mSendingBuf[MAX_SOCK_SENDBUF]; //데이터 버퍼	
};