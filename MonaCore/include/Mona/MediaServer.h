/*
This file is a part of MonaSolutions Copyright 2017
mathieu.poux[a]gmail.com
jammetthomas[a]gmail.com

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License received along this program for more
details (or else see http://www.gnu.org/licenses/).

*/

#pragma once

#include "Mona/Mona.h"
#include "Mona/MediaWriter.h"
#include "Mona/SRT.h"

namespace Mona {


struct MediaServer : Media::Stream, Media::Targets, virtual Object {

	enum Type {
		TYPE_TCP = 1 // to match Media::Stream::Type
#if defined(SRT_API)
		,TYPE_SRT = 2 // to match Media::Stream::Type
#endif
	};
	
	MediaServer(Type type, const Path& path, unique<MediaWriter>&& pWriter, const SocketAddress& address, IOSocket& io, const shared<TLS>& pTLS = nullptr);
	virtual ~MediaServer() { stop(); }

	static unique<MediaServer> New(Type type, const Path& path, const char* subMime, const SocketAddress& address, IOSocket& io, const shared<TLS>& pTLS = nullptr);
	static unique<MediaServer> New(Type type, const Path& path, const SocketAddress& address, IOSocket& io, const shared<TLS>& pTLS = nullptr) { return New(type, path, path.extension().c_str(), address, io, pTLS); }

	bool running() const { return _running; }
	
	const SocketAddress		address;
	IOSocket&				io;
	const shared<Socket>&	socket();
	Socket*					operator->() { return socket().get(); }

private:
	void starting(const Parameters& parameters);
	void stopping();

	std::string& buildDescription(std::string& description) { return String::Assign(description, "Stream server ", TypeToString(type), "://", address, path, '|', _subMime); }
		
	Socket::OnAccept			_onConnnection;
	Socket::OnError				_onError;

	shared<Socket>				_pSocket;
	shared<TLS>					_pTLS;
	const char*					_subMime;
	bool						_running;
	std::set<shared<Media::Stream>> _streams;

};

} // namespace Mona
