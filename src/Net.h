// ShaderToyFX - requêtes HTTPS (WinHTTP)
#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace stfx
{
struct HttpResult
{
	int status = 0;
	std::vector< uint8_t > body;
	std::string error;
	bool Ok() const { return error.empty() && status >= 200 && status < 300; }
	std::string Text() const { return std::string( body.begin(), body.end() ); }
};

// GET synchrone (à appeler depuis un thread de travail, jamais depuis le thread de rendu)
HttpResult HttpGet( const std::string& url, int timeoutMs = 15000 );

std::string UrlEncode( const std::string& s );
}//namespace stfx
