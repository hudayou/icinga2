/******************************************************************************
 * Icinga 2                                                                   *
 * Copyright (C) 2012-2015 Icinga Development Team (http://www.icinga.org)    *
 *                                                                            *
 * This program is free software; you can redistribute it and/or              *
 * modify it under the terms of the GNU General Public License                *
 * as published by the Free Software Foundation; either version 2             *
 * of the License, or (at your option) any later version.                     *
 *                                                                            *
 * This program is distributed in the hope that it will be useful,            *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of             *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the              *
 * GNU General Public License for more details.                               *
 *                                                                            *
 * You should have received a copy of the GNU General Public License          *
 * along with this program; if not, write to the Free Software Foundation     *
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.             *
 ******************************************************************************/

#include "remote/jsonrpcconnection.hpp"
#include "remote/apilistener.hpp"
#include "remote/apifunction.hpp"
#include "remote/jsonrpc.hpp"
#include "base/dynamictype.hpp"
#include "base/objectlock.hpp"
#include "base/utility.hpp"
#include "base/logger.hpp"
#include "base/exception.hpp"
#include <boost/thread/once.hpp>

using namespace icinga;

static boost::once_flag l_HttpConnectionOnceFlag = BOOST_ONCE_INIT;
static Timer::Ptr l_HttpConnectionTimeoutTimer;

HttpConnection::HttpConnection(const String& identity, bool authenticated, const TlsStream::Ptr& stream)
	: m_Stream(stream), m_Seen(Utility::GetTime()), m_ProcessingRequest(false)
{
	boost::call_once(l_HttpConnectionOnceFlag, &HttpConnection::StaticInitialize);

//	if (authenticated)
//		m_ApiUser = ApiUser::GetByName(identity);
}

void HttpConnection::StaticInitialize(void)
{
	l_HttpConnectionTimeoutTimer = new Timer();
	l_HttpConnectionTimeoutTimer->OnTimerExpired.connect(boost::bind(&HttpConnection::TimeoutTimerHandler));
	l_HttpConnectionTimeoutTimer->SetInterval(15);
	l_HttpConnectionTimeoutTimer->Start();
}

void HttpConnection::Start(void)
{
	m_Stream->RegisterDataHandler(boost::bind(&HttpConnection::DataAvailableHandler, this));
	DataAvailableHandler();
}

Object::Ptr HttpConnection::GetApiUser(void) const
{
	return m_ApiUser;
}

TlsStream::Ptr HttpConnection::GetStream(void) const
{
	return m_Stream;
}

void HttpConnection::Disconnect(void)
{
	Log(LogDebug, "HttpConnection", "Http client disconnected");

	ApiListener::Ptr listener = ApiListener::GetInstance();
	listener->RemoveHttpClient(this);

	m_Stream->Close();
}

bool HttpConnection::ProcessMessage(void)
{
	String line;

	StreamReadStatus srs = m_Stream->ReadLine(&line, m_Context, false);

	if (srs != StatusNewItem)
		return false;

	Log(LogInformation, "HttpConnection", line);

	if (line != "")
		return true;

	m_Seen = Utility::GetTime();

	Log(LogInformation, "HttpConnection", "Processing Http message");

	String msg = "HTTP/1.1 200 OK\nContent-Length: 12\n\nHello World!";
	m_Stream->Write(msg.CStr(), msg.GetLength());

	/* TODO: Process request */

	return true;
}

void HttpConnection::DataAvailableHandler(void)
{
	try {
		while (ProcessMessage())
			; /* empty loop body */
	} catch (const std::exception& ex) {
		Log(LogWarning, "HttpConnection")
		    << "Error while reading Http request: " << DiagnosticInformation(ex);

		Disconnect();
	}
}

void HttpConnection::CheckLiveness(void)
{
	if (m_Seen < Utility::GetTime() - 10 && !m_ProcessingRequest) {
		Log(LogDebug, "HttpConnection")
		    <<  "No messages for Http connection have been received in the last 10 seconds.";
		Disconnect();
	}
}

void HttpConnection::TimeoutTimerHandler(void)
{
	ApiListener::Ptr listener = ApiListener::GetInstance();

	BOOST_FOREACH(const HttpConnection::Ptr& client, listener->GetHttpClients()) {
		client->CheckLiveness();
	}
}
