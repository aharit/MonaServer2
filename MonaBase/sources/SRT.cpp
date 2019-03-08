/*
This file is a part of MonaSolutions Copyright 2017
mathieu.poux[a]gmail.com
jammetthomas[a]gmail.com

This program is free software: you can redistribute it and/or
modify it under the terms of the the Mozilla Public License v2.0.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
Mozilla Public License v. 2.0 received along this program for more
details (or else see http://mozilla.org/MPL/2.0/).

*/


#include "Mona/SRT.h"

#if defined(SRT_API)

using namespace std;

namespace Mona {

SRT::SRT() {
	if (::srt_startup())
		CRITIC("SRT startup, ", ::srt_getlasterror_str());
	::srt_setloghandler(nullptr, Log);
	::srt_setloglevel(4);
}

SRT::~SRT() {
	::srt_setloghandler(nullptr, nullptr);
	::srt_cleanup();
}

void SRT::Log(void* opaque, int level, const char* file, int line, const char* area, const char* message) {
	if (!Logs::GetLevel() || level>4)
		return;
	static LOG_LEVEL levels[5] = { LOG_CRITIC, LOG_ERROR, LOG_WARN, LOG_INFO, LOG_TRACE};
	Logs::Log(levels[level], file, line, area, ", ", message);
}

SRT::Stats& SRT::Stats::Null() {
	static struct Null : Stats, virtual Object {
		double bandwidthEstimated() const { return 0; }
		double bandwidthMaxUsed() const { return 0; }
		UInt32 bufferTime() const { return 0; }
		double negotiatedLatency() const { return 0; }
		UInt32 recvLostCount() const { return 0; }
		UInt32 sendLostCount() const { return 0; }
		double retransmitRate() const { return 0; }
		double rtt() const { return 0; }
	} Null;
	return Null;
}

int SRT::GetError(int error) {
	switch (error) {
		case SRT_EUNKNOWN:
		//case SRT_SUCCESS:
			return NET_ENOTSUP;

		case SRT_ECONNSETUP:
		case SRT_ENOSERVER:
		case SRT_ECONNREJ:
			return NET_ECONNREFUSED;
		case SRT_ESOCKFAIL:
		case SRT_ESECFAIL:
			return NET_ENOTSUP;

		case SRT_ECONNFAIL:
		case SRT_ECONNLOST:
			return NET_ESHUTDOWN;
		case SRT_ENOCONN:
			return NET_ENOTCONN;

		case SRT_ERESOURCE:
		case SRT_ETHREAD:
		case SRT_ENOBUF:
			return NET_ENOTSUP;

		case SRT_EFILE:
		case SRT_EINVRDOFF:
		case SRT_ERDPERM:
		case SRT_EINVWROFF:
		case SRT_EWRPERM:
			return NET_ENOTSUP;

		case SRT_EINVOP:
		case SRT_EBOUNDSOCK:
		case SRT_ECONNSOCK:
		case SRT_EINVPARAM:
			return NET_EINVAL;
		case SRT_EINVSOCK:
			return NET_ENOTSOCK;
		case SRT_EUNBOUNDSOCK:
		case SRT_ENOLISTEN:
		case SRT_ERDVNOSERV:
		case SRT_ERDVUNBOUND:
		case SRT_EINVALMSGAPI:
		case SRT_EINVALBUFFERAPI:
		case SRT_EDUPLISTEN:
		case SRT_ELARGEMSG:
		case SRT_EINVPOLLID:
			return NET_ENOTSUP;

		case SRT_EASYNCFAIL:
		case SRT_EASYNCSND:
		case SRT_EASYNCRCV:
			return NET_EWOULDBLOCK;
		case SRT_ETIMEOUT:
			return NET_ETIMEDOUT;
		case SRT_ECONGEST:

		case SRT_EPEERERR:
			return NET_ENOTSUP;
		
	}
	return (int)error; // not found
}

SRT::Socket::Socket() : Mona::Socket(TYPE_SRT), _shutdownRecv(false), _available(SRT_LIVE_DEF_PLSIZE) {
	_id = ::srt_socket(AF_INET, SOCK_DGRAM, 0); // TODO: This is ipv4 for now, we must find a way to set the family when creating the socket

	/*int opt = 1;
	::srt_setsockflag(_id, ::SRTO_SENDER, &opt, sizeof opt);*/
}

SRT::Socket::Socket(const sockaddr& addr, SRTSOCKET id) : Mona::Socket(id, addr, Socket::TYPE_SRT), _shutdownRecv(false), _available(SRT_LIVE_DEF_PLSIZE) { }

SRT::Socket::~Socket() {
	if (_id == ::SRT_INVALID_SOCK)
		return;
	// gracefull disconnection => flush + shutdown + close
	/// shutdown + flush
	Exception ignore;
	flush(ignore, true);

	close();
}

UInt32 SRT::Socket::available() const {
	UInt32 value = _available;
	if (!value)
		_available = SRT_LIVE_DEF_PLSIZE; // reset to SRT buffer size
	return value;
}

bool SRT::Socket::bind(Exception& ex, const SocketAddress& address) {
	if (_id == ::SRT_INVALID_SOCK) {
		SetException(ex, NET_ESHUTDOWN);
		return false;
	}

	union {
		struct sockaddr_in  sa_in;
		struct sockaddr_in6 sa_in6;
	} addr;
	int addrSize = address.family() == IPAddress::IPv6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
	memcpy(&addr, address.data(), addrSize);
	sockaddr* pAdd = reinterpret_cast<sockaddr*>(&addr);
	pAdd->sa_family = address.family() == IPAddress::IPv6 ? AF_INET6 : AF_INET;
	if (::srt_bind(_id, pAdd, addrSize)) {
		SetException(ex, GetError(), " (address=", address, ")");
		return false;
	}

	if (address)
		_address = address; // if port = 0, will be computed!
	else
		_address.set(IPAddress::Loopback(), 0); // to advise that address must be computed

	return true;
}

bool SRT::Socket::listen(Exception& ex, int backlog) {
	if (_id == ::SRT_INVALID_SOCK) {
		SetException(ex, NET_ESHUTDOWN);
		return false;
	}

	if (::srt_listen(_id, backlog) == 0) {
		_listening = true;
		return true;
	}
	SetException(ex, GetError(), " (backlog=", backlog, ")");
	return false;
}

bool SRT::Socket::setNonBlockingMode(Exception& ex, bool value) {
	if (_id == ::SRT_INVALID_SOCK) {
		SetException(ex, NET_ESHUTDOWN, "SRT setNonBlockingMode");
		return false;
	}

	bool block = !value;
	if (::srt_setsockflag(_id, SRTO_SNDSYN, &block, sizeof(block)) != 0) {
		SetException(ex, GetError(), " ", ::srt_getlasterror_str());
		return false;
	}
	if (::srt_setsockflag(_id, SRTO_RCVSYN, &block, sizeof(block)) != 0) {
		SetException(ex, GetError(), " ", ::srt_getlasterror_str());
		return false;
	}
	
	return _nonBlockingMode = value;
}

const SocketAddress& SRT::Socket::address() const {
	if (_address && !_address.port()) {
		// computable!
		union {
			struct sockaddr_in  sa_in;
			struct sockaddr_in6 sa_in6;
		} addr;
		int addrSize = sizeof(addr);
		if (::srt_getsockname(_id, (sockaddr*)&addr, &addrSize) == 0)
			_address.set((sockaddr&)addr);
	}
	return _address;
}

bool SRT::Socket::accept(Exception& ex, shared<Mona::Socket>& pSocket) {
	if (_id == ::SRT_INVALID_SOCK) {
		SetException(ex, NET_ESHUTDOWN);
		return false;
	}

	sockaddr_in6 scl;
	int sclen = sizeof scl;
	::SRTSOCKET newSocket = ::srt_accept(_id, (sockaddr*)&scl, &sclen);
	if (newSocket == SRT_INVALID_SOCK) {
		if (::srt_getlasterror(NULL) == SRT_EASYNCRCV) { // not an error
			SetException(ex, NET_EWOULDBLOCK);
			return false;
		}
		SetException(ex, GetError(), " ", ::srt_getlasterror_str());
		return false;
	}

	pSocket.set<SRT::Socket>((sockaddr&)scl, newSocket);
	return true;
}

bool SRT::Socket::connect(Exception& ex, const SocketAddress& address, UInt16 timeout) {
	if (_id == ::SRT_INVALID_SOCK) {
		SetException(ex, NET_ESHUTDOWN);
		return false;
	}

	bool block = false;
	if (timeout && !_nonBlockingMode) {
		block = true;
		if (!setNonBlockingMode(ex, true))
			return false;
	}

	union {
		struct sockaddr_in  sa_in;
		struct sockaddr_in6 sa_in6;
	} addr;
	int addrSize = address.family() == IPAddress::IPv6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
	memcpy(&addr, address.data(), addrSize);
	sockaddr* pAdd = reinterpret_cast<sockaddr*>(&addr);
	pAdd->sa_family = address.family() == IPAddress::IPv6 ? AF_INET6 : AF_INET;
	
	int rc, error(0);
	rc = ::srt_connect(_id, pAdd, addrSize);
	if (rc) {
		SetException(ex, GetError(), " (address=", address, ")");
		return false; // fail
	}

	_address.set(IPAddress::Loopback(), 0); // to advise that address must be computed
	_peerAddress = address;
	return error ? false : true;
}

int SRT::Socket::receive(Exception& ex, void* buffer, UInt32 size, int flags, SocketAddress* pAddress) {
	if (_id == ::SRT_INVALID_SOCK) {
		SetException(ex, NET_ESHUTDOWN);
		return false;
	}

	// assign pAddress (no other way possible here)
	if(pAddress)
		pAddress->set(peerAddress());

	// Read the message
	int result = ::srt_recvmsg(_id, STR buffer, size); // /!\ SRT expect at least a buffer of 1316 bytes
	if (result == SRT_ERROR) {
		int error = ::srt_getlasterror(NULL);
		if (error == SRT_EASYNCRCV) { // EAGAIN, wait for reception to be ready
			SetException(ex, NET_EWOULDBLOCK, " (address=", pAddress ? *pAddress : _peerAddress, ", size=", size, ", flags=", flags, ")");
			_available = 0; // to avoid an infinite loop of receive call, we cannot know the available data with SRT
		}
		else
			SetException(ex, GetError(error), " ", ::srt_getlasterror_str(), " (address=", pAddress ? *pAddress : _peerAddress, ", size=", size, ", flags=", flags, ")");
		return -1;
	}
	Mona::Socket::receive(result);

	if (!_address)
		_address.set(IPAddress::Loopback(), 0); // to advise that address is computable

	return result;
}

int SRT::Socket::sendTo(Exception& ex, const void* data, UInt32 size, const SocketAddress& address, int flags) {
	if (_id == ::SRT_INVALID_SOCK) {
		SetException(ex, NET_ESHUTDOWN);
		return false;
	}

	if (!size || size > ::SRT_LIVE_DEF_PLSIZE) {
		SetException(ex, NET_EINVAL, " SRT: data sent must be in the range [1,1316]");
		return -1;
	}

	/*size_t chunk = 0;
	for (size_t i = 0; i < size; i += chunk) {
		chunk = min<size_t>(size - i, (size_t)SRT_LIVE_DEF_PLSIZE);
		if (::srt_send(_id, ((STR data) + i), chunk) < 0) {
			ex.set<Ex::Net::Socket>("SRT: send error; ", " ", ::srt_getlasterror_str());
			return -1;
		}
	}*/
	int sent = ::srt_send(_id, STR data, size);
	if (sent <= 0) {
		SetException(ex, GetError(), " ", ::srt_getlasterror_str());
		return -1;
	}

	Mona::Socket::send(sent);

	if (!_address)
		_address.set(IPAddress::Loopback(), 0); // to advise that address is computable

	return sent;
}

bool SRT::Socket::close(Socket::ShutdownType type) {
	if (type == Socket::SHUTDOWN_RECV) {
		_shutdownRecv = true;
		return true;
	}
	if (::srt_close(_id) < 0)
		return false;
	return true;
}

bool SRT::Socket::setRecvBufferSize(Exception& ex, int size) {
	if (!setOption(ex, ::SRTO_UDP_RCVBUF, size))
		return false;
	_recvBufferSize = size;
	return true;
}
bool SRT::Socket::setSendBufferSize(Exception& ex, int size) {
	if (!setOption(ex, ::SRTO_UDP_SNDBUF, size))
		return false;
	_sendBufferSize = size;
	return true;
}

bool SRT::Socket::setPassphrase(Exception& ex, const char* data, UInt32 size) {
	if (_id == ::SRT_INVALID_SOCK) {
		SetException(ex, NET_ESHUTDOWN);
		return false;
	}
	
	if (::srt_setsockflag(_id, SRTO_PASSPHRASE, data, size) != -1)
		return true;
	SetException(ex, GetError(), " ", ::srt_getlasterror_str());
	return false;
}

bool SRT::Socket::setLinger(Exception& ex, bool on, int seconds) {
	if (_id == ::SRT_INVALID_SOCK) {
		SetException(ex, NET_ESHUTDOWN);
		return false;
	}
	struct linger l;
	l.l_onoff = on ? 1 : 0;
	l.l_linger = seconds;
	return setOption(ex, ::SRTO_LINGER, l);
}

bool SRT::Socket::getLinger(Exception& ex, bool& on, int& seconds) const {
	if (_id == ::SRT_INVALID_SOCK) {
		SetException(ex, NET_ESHUTDOWN);
		return false;
	}
	struct linger l;
	if (!getOption(ex, ::SRTO_LINGER, l))
		return false;
	seconds = l.l_linger;
	on = l.l_onoff != 0;
	return true;
}

bool SRT::Socket::getStats(Exception& ex, SRT_TRACEBSTATS* perf) const {
	if (_id == ::SRT_INVALID_SOCK) {
		SetException(ex, NET_ESHUTDOWN);
		return false;
	}
	if (::srt_bistats(_id, perf, 0, 0) != -1)
		return true;
	SetException(ex, GetError(), " ", ::srt_getlasterror_str());
	return false;
}

}  // namespace Mona

#endif
