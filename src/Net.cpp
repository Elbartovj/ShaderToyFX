#include "Net.h"
#include "Util.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace stfx
{
std::string UrlEncode( const std::string& s )
{
	static const char* hex = "0123456789ABCDEF";
	std::string r;
	for( unsigned char c : s )
	{
		if( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '-' || c == '_' || c == '.' || c == '~' )
			r += (char)c;
		else
		{
			r += '%';
			r += hex[ c >> 4 ];
			r += hex[ c & 15 ];
		}
	}
	return r;
}

#ifdef _WIN32
namespace
{
struct HInternet
{
	HINTERNET h = nullptr;
	HInternet( HINTERNET v = nullptr ) : h( v ) {}
	~HInternet()
	{
		if( h )
			WinHttpCloseHandle( h );
	}
	operator HINTERNET() const { return h; }
};

std::string LastErr( const char* what )
{
	return std::string( what ) + " (WinHTTP " + std::to_string( GetLastError() ) + ")";
}
}//namespace

HttpResult HttpGet( const std::string& url, int timeoutMs )
{
	HttpResult res;
	std::wstring wurl = Widen( url );

	URL_COMPONENTS uc{};
	uc.dwStructSize      = sizeof( uc );
	wchar_t host[ 256 ]  = {};
	wchar_t path[ 4096 ] = {};
	uc.lpszHostName      = host;
	uc.dwHostNameLength  = 256;
	uc.lpszUrlPath       = path;
	uc.dwUrlPathLength   = 4096;
	wchar_t extra[ 4096 ] = {};
	uc.lpszExtraInfo      = extra;
	uc.dwExtraInfoLength  = 4096;
	if( !WinHttpCrackUrl( wurl.c_str(), 0, 0, &uc ) )
	{
		res.error = "URL invalide : " + url;
		return res;
	}
	std::wstring object = std::wstring( path, uc.dwUrlPathLength ) + std::wstring( extra, uc.dwExtraInfoLength );

	HInternet session( WinHttpOpen( L"ShaderToyFX/1.0 (Resolume FFGL)", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 ) );
	if( !session )
		session.h = WinHttpOpen( L"ShaderToyFX/1.0 (Resolume FFGL)", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 );
	if( !session )
	{
		res.error = LastErr( "WinHttpOpen" );
		return res;
	}
	WinHttpSetTimeouts( session, timeoutMs, timeoutMs, timeoutMs, timeoutMs );
#ifdef WINHTTP_OPTION_DECOMPRESSION
	DWORD decomp = WINHTTP_DECOMPRESSION_FLAG_ALL;
	WinHttpSetOption( session, WINHTTP_OPTION_DECOMPRESSION, &decomp, sizeof( decomp ) );
#endif

	HInternet connect( WinHttpConnect( session, host, uc.nPort, 0 ) );
	if( !connect )
	{
		res.error = LastErr( "WinHttpConnect" );
		return res;
	}
	DWORD flags = ( uc.nScheme == INTERNET_SCHEME_HTTPS ) ? WINHTTP_FLAG_SECURE : 0;
	HInternet request( WinHttpOpenRequest( connect, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags ) );
	if( !request )
	{
		res.error = LastErr( "WinHttpOpenRequest" );
		return res;
	}
	const wchar_t* headers = L"Accept: */*\r\n";
	if( !WinHttpSendRequest( request, headers, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) || !WinHttpReceiveResponse( request, nullptr ) )
	{
		res.error = LastErr( "Connexion impossible" );
		return res;
	}

	DWORD status = 0, size = sizeof( status );
	WinHttpQueryHeaders( request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX );
	res.status = (int)status;

	for( ;; )
	{
		DWORD avail = 0;
		if( !WinHttpQueryDataAvailable( request, &avail ) )
		{
			res.error = LastErr( "Lecture" );
			break;
		}
		if( avail == 0 )
			break;
		size_t old = res.body.size();
		res.body.resize( old + avail );
		DWORD read = 0;
		if( !WinHttpReadData( request, res.body.data() + old, avail, &read ) )
		{
			res.error = LastErr( "Lecture" );
			break;
		}
		res.body.resize( old + read );
	}
	if( res.error.empty() && ( res.status < 200 || res.status >= 300 ) )
		res.error = "HTTP " + std::to_string( res.status );
	return res;
}
#else
HttpResult HttpGet( const std::string& url, int )
{
	HttpResult r;
	r.error = "HTTP non supporté sur cette plateforme";
	return r;
}
#endif
}//namespace stfx
