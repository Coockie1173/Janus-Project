#include "update.h"
#include "files.h"
#include "ge_version.h"
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ge
{
	bool unzip_into(const std::string& zip, const std::string& folder);
}

namespace launcher
{
namespace
{
	const char* kApi{ "https://api.github.com/repos/Coockie1173/Janus-Project" };
	const char* kTag{ "latest" };
#ifdef _WIN32
	const char* kPackage{ "Janus-Project-win64.zip" };
	const char* kExecutable{ "Janus-Project.exe" };
#else
	const char* kPackage{ "Janus-Project-linux64.zip" };
	const char* kExecutable{ "Janus-Project" };
#endif
	//everything the updater keeps: the downloads, the new files, the old ones moved aside
	const char* kWork{ "update" };
	const int kCheckSeconds{ 4 };
	const int kSocketSeconds{ 15 };
	const size_t kMostChanges{ 12 };

	//what of the package is the player's: their bindings, the disc and the packs (those are asked about)
	bool PlayersOwn(const std::string& Path)
	{
		return Path == "keys.txt" || Path.rfind("game/", 0) == 0 || Path.rfind("modpacks/", 0) == 0;
	}

	//just enough json for GitHub's answers: objects, arrays, strings, the rest as text
	struct Json
	{
		enum class Kind { Null, Text, Number, Object, Array } Type{};
		std::string Text{};
		std::map<std::string, Json> Fields{};
		std::vector<Json> Items{};

		const Json& operator[](const std::string& Key) const
		{
			static const Json None{};
			const auto Found{ Fields.find(Key) };
			return Found != Fields.end() ? Found->second : None;
		}
	};

	class JsonReader
	{
	public:
		explicit JsonReader(const std::string& Text) : m_Text{ Text } {}

		bool Read(Json& Out)
		{
			return Value(Out, 0);
		}

	private:
		const std::string& m_Text;
		size_t m_At{};

		void Space()
		{
			while (m_At < m_Text.size() && std::strchr(" \t\r\n", m_Text[m_At]) != nullptr)
			{
				++m_At;
			}
		}

		void Utf8(uint32_t c, std::string& Out)
		{
			if (c < 0x80)
			{
				Out += (char)c;
			}
			else if (c < 0x800)
			{
				Out += (char)(0xC0 | c >> 6);
				Out += (char)(0x80 | (c & 0x3F));
			}
			else if (c < 0x10000)
			{
				Out += (char)(0xE0 | c >> 12);
				Out += (char)(0x80 | (c >> 6 & 0x3F));
				Out += (char)(0x80 | (c & 0x3F));
			}
			else
			{
				Out += (char)(0xF0 | c >> 18);
				Out += (char)(0x80 | (c >> 12 & 0x3F));
				Out += (char)(0x80 | (c >> 6 & 0x3F));
				Out += (char)(0x80 | (c & 0x3F));
			}
		}

		bool Hex4(uint32_t& Out)
		{
			if (m_At + 4 > m_Text.size())
			{
				return false;
			}
			Out = (uint32_t)std::strtoul(m_Text.substr(m_At, 4).c_str(), nullptr, 16);
			m_At += 4;
			return true;
		}

		bool String(std::string& Out)
		{
			++m_At; //the opening quote
			while (m_At < m_Text.size() && m_Text[m_At] != '"')
			{
				const char c{ m_Text[m_At++] };
				if (c != '\\')
				{
					Out += c;
					continue;
				}
				if (m_At >= m_Text.size())
				{
					return false;
				}
				const char e{ m_Text[m_At++] };
				switch (e)
				{
				case 'n': Out += '\n'; break;
				case 't': Out += '\t'; break;
				case 'r': Out += '\r'; break;
				case 'b': Out += '\b'; break;
				case 'f': Out += '\f'; break;
				case 'u':
				{
					uint32_t c1{};
					if (!Hex4(c1))
					{
						return false;
					}
					//a surrogate pair is one character
					if (c1 >= 0xD800 && c1 < 0xDC00 && m_Text.compare(m_At, 2, "\\u") == 0)
					{
						m_At += 2;
						uint32_t c2{};
						if (!Hex4(c2))
						{
							return false;
						}
						c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
					}
					Utf8(c1, Out);
					break;
				}
				default: Out += e; break;
				}
			}
			++m_At;
			return m_At <= m_Text.size();
		}

		bool Value(Json& Out, int Depth)
		{
			Space();
			if (m_At >= m_Text.size() || Depth > 64)
			{
				return false;
			}
			const char c{ m_Text[m_At] };
			if (c == '"')
			{
				Out.Type = Json::Kind::Text;
				return String(Out.Text);
			}
			if (c == '{' || c == '[')
			{
				const bool Object{ c == '{' };
				Out.Type = Object ? Json::Kind::Object : Json::Kind::Array;
				++m_At;
				Space();
				if (m_At < m_Text.size() && m_Text[m_At] == (Object ? '}' : ']'))
				{
					++m_At;
					return true;
				}
				while (true)
				{
					std::string Key;
					if (Object)
					{
						Space();
						if (m_At >= m_Text.size() || m_Text[m_At] != '"' || !String(Key))
						{
							return false;
						}
						Space();
						if (m_At >= m_Text.size() || m_Text[m_At++] != ':')
						{
							return false;
						}
					}
					Json Item;
					if (!Value(Item, Depth + 1))
					{
						return false;
					}
					if (Object)
					{
						Out.Fields[Key] = std::move(Item);
					}
					else
					{
						Out.Items.push_back(std::move(Item));
					}
					Space();
					if (m_At >= m_Text.size())
					{
						return false;
					}
					const char Next{ m_Text[m_At++] };
					if (Next == (Object ? '}' : ']'))
					{
						return true;
					}
					if (Next != ',')
					{
						return false;
					}
				}
			}
			//a number, true, false or null: its text
			const size_t From{ m_At };
			while (m_At < m_Text.size() && std::strchr(",}] \t\r\n", m_Text[m_At]) == nullptr)
			{
				++m_At;
			}
			Out.Text = m_Text.substr(From, m_At - From);
			Out.Type = Out.Text == "null" ? Json::Kind::Null : Json::Kind::Number;
			return m_At > From;
		}
	};

	//https only: GitHub and where its downloads lead
	struct Url
	{
		std::string Host{};
		std::string Path{};
	};

	bool ParseUrl(const std::string& Text, Url& Out)
	{
		const std::string Scheme{ "https://" };
		if (Text.rfind(Scheme, 0) != 0)
		{
			return false;
		}
		const size_t Slash{ Text.find('/', Scheme.size()) };
		Out.Host = Text.substr(Scheme.size(), Slash == std::string::npos ? std::string::npos : Slash - Scheme.size());
		Out.Path = Slash == std::string::npos ? "/" : Text.substr(Slash);
		return !Out.Host.empty() && Out.Host.find(':') == std::string::npos;
	}

#ifdef _WIN32
	using Socket = SOCKET;
	const Socket kNoSocket{ INVALID_SOCKET };
	void CloseSocket(Socket s) { closesocket(s); }
	void Blocking(Socket s, bool On) { u_long Off{ On ? 0u : 1u }; ioctlsocket(s, FIONBIO, &Off); }
#else
	using Socket = int;
	const Socket kNoSocket{ -1 };
	void CloseSocket(Socket s) { close(s); }
	void Blocking(Socket s, bool On) { const int Flags{ fcntl(s, F_GETFL, 0) }; fcntl(s, F_SETFL, On ? Flags & ~O_NONBLOCK : Flags | O_NONBLOCK); }
#endif

	//a connection to port 443 that gives up after a while rather than hanging the start
	Socket Connect(const std::string& Host, std::string& Error)
	{
#ifdef _WIN32
		static const bool Started{ []
			{
				WSADATA Data;
				return WSAStartup(MAKEWORD(2, 2), &Data) == 0;
			}() };
		(void)Started;
#endif
		addrinfo Hints{};
		Hints.ai_family = AF_UNSPEC;
		Hints.ai_socktype = SOCK_STREAM;
		addrinfo* pFound{};
		if (getaddrinfo(Host.c_str(), "443", &Hints, &pFound) != 0 || pFound == nullptr)
		{
			Error = "can't find " + Host + " (offline?)";
			return kNoSocket;
		}
		Socket s{ kNoSocket };
		for (addrinfo* p{ pFound }; p != nullptr && s == kNoSocket; p = p->ai_next)
		{
			s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
			if (s == kNoSocket)
			{
				continue;
			}
			Blocking(s, false);
			connect(s, p->ai_addr, (int)p->ai_addrlen);
			fd_set Write, Fail;
			FD_ZERO(&Write);
			FD_ZERO(&Fail);
			FD_SET(s, &Write);
			FD_SET(s, &Fail);
			timeval Wait{ kSocketSeconds, 0 };
			int Problem{};
			socklen_t Size{ sizeof Problem };
			if (select((int)s + 1, nullptr, &Write, &Fail, &Wait) != 1 || !FD_ISSET(s, &Write)
				|| getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&Problem, &Size) != 0 || Problem != 0)
			{
				CloseSocket(s);
				s = kNoSocket;
				continue;
			}
			Blocking(s, true);
#ifdef _WIN32
			const DWORD Timeout{ kSocketSeconds * 1000 };
#else
			const timeval Timeout{ kSocketSeconds, 0 };
#endif
			setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&Timeout, sizeof Timeout);
			setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (const char*)&Timeout, sizeof Timeout);
		}
		freeaddrinfo(pFound);
		if (s == kNoSocket)
		{
			Error = "can't reach " + Host;
		}
		return s;
	}

	//the system's certificates: windows' own store (a static openssl knows of none), on linux
	//openssl's and the usual bundles
	SSL_CTX* Context()
	{
		static SSL_CTX* pContext{ []
			{
				SSL_CTX* p{ SSL_CTX_new(TLS_client_method()) };
				if (p == nullptr)
				{
					return p;
				}
				SSL_CTX_set_min_proto_version(p, TLS1_2_VERSION);
				SSL_CTX_set_verify(p, SSL_VERIFY_PEER, nullptr);
#ifdef SSL_OP_IGNORE_UNEXPECTED_EOF
				SSL_CTX_set_options(p, SSL_OP_IGNORE_UNEXPECTED_EOF);
#endif
#ifdef _WIN32
				X509_STORE* pStore{ SSL_CTX_get_cert_store(p) };
				if (HCERTSTORE System{ CertOpenSystemStoreW(0, L"ROOT") })
				{
					for (PCCERT_CONTEXT pCert{ CertEnumCertificatesInStore(System, nullptr) }; pCert != nullptr;
						pCert = CertEnumCertificatesInStore(System, pCert))
					{
						const unsigned char* pBytes{ pCert->pbCertEncoded };
						if (X509* pX509{ d2i_X509(nullptr, &pBytes, (long)pCert->cbCertEncoded) })
						{
							X509_STORE_add_cert(pStore, pX509);
							X509_free(pX509);
						}
					}
					CertCloseStore(System, 0);
				}
#else
				SSL_CTX_set_default_verify_paths(p);
				for (const char* pBundle : { "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
					"/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem" })
				{
					if (std::filesystem::exists(pBundle))
					{
						SSL_CTX_load_verify_locations(p, pBundle, nullptr);
					}
				}
#endif
				return p;
			}() };
		return pContext;
	}

	//a GET over https, redirects followed, the body handed over as it comes. Sink returning false stops it
	bool Get(std::string Address, const char* pAccept, const std::function<bool(const char*, size_t)>& Sink, std::string& Error)
	{
		for (int Hop{ 0 }; Hop < 6; ++Hop)
		{
			Url Where;
			if (!ParseUrl(Address, Where))
			{
				Error = "not an https address: " + Address;
				return false;
			}
			SSL_CTX* pContext{ Context() };
			if (pContext == nullptr)
			{
				Error = "no TLS";
				return false;
			}
			const Socket s{ Connect(Where.Host, Error) };
			if (s == kNoSocket)
			{
				return false;
			}
			std::unique_ptr<SSL, decltype(&SSL_free)> pSsl{ SSL_new(pContext), SSL_free };
			SSL_set_fd(pSsl.get(), (int)s);
			SSL_set_tlsext_host_name(pSsl.get(), Where.Host.c_str());
			SSL_set1_host(pSsl.get(), Where.Host.c_str());
			auto Fail{ [&](const std::string& Why)
				{
					Error = Why;
					pSsl.reset();
					CloseSocket(s);
					return false;
				} };
			if (SSL_connect(pSsl.get()) != 1)
			{
				const long Verify{ SSL_get_verify_result(pSsl.get()) };
				return Fail(Where.Host + ": no secure connection" + (Verify != X509_V_OK ? std::string(" (") + X509_verify_cert_error_string(Verify) + ")" : ""));
			}
			const std::string Request{ "GET " + Where.Path + " HTTP/1.1\r\nHost: " + Where.Host + "\r\nUser-Agent: Janus-Project-updater\r\nAccept: "
				+ pAccept + "\r\nConnection: close\r\n\r\n" };
			if (SSL_write(pSsl.get(), Request.data(), (int)Request.size()) != (int)Request.size())
			{
				return Fail(Where.Host + ": the request didn't go out");
			}

			std::string Buffer;
			auto Fill{ [&]
				{
					char Chunk[16384];
					const int Got{ SSL_read(pSsl.get(), Chunk, sizeof Chunk) };
					if (Got <= 0)
					{
						return false;
					}
					Buffer.append(Chunk, (size_t)Got);
					return true;
				} };
			size_t HeadEnd{};
			while ((HeadEnd = Buffer.find("\r\n\r\n")) == std::string::npos)
			{
				if (!Fill() || Buffer.size() > 65536)
				{
					return Fail(Where.Host + ": no answer");
				}
			}
			std::string Head{ Buffer.substr(0, HeadEnd + 2) };
			Buffer.erase(0, HeadEnd + 4);
			const int Status{ Head.size() > 12 ? std::atoi(Head.c_str() + 9) : 0 };
			std::map<std::string, std::string> Headers;
			for (size_t At{ Head.find("\r\n") + 2 }; At < Head.size();)
			{
				const size_t End{ Head.find("\r\n", At) };
				const std::string Line{ Head.substr(At, End - At) };
				const size_t Colon{ Line.find(':') };
				if (Colon != std::string::npos)
				{
					std::string Name{ Line.substr(0, Colon) };
					for (char& c : Name)
					{
						c = (char)std::tolower((unsigned char)c);
					}
					Headers[Name] = Line.substr(Line.find_first_not_of(' ', Colon + 1) == std::string::npos ? Line.size() : Line.find_first_not_of(' ', Colon + 1));
				}
				At = End + 2;
			}
			if (Status >= 300 && Status < 400 && Headers.count("location"))
			{
				Address = Headers["location"];
				pSsl.reset();
				CloseSocket(s);
				continue;
			}
			if (Status != 200)
			{
				return Fail(Where.Host + " answered " + std::to_string(Status) + (Status == 403 ? " (asked too often, try later)" : ""));
			}

			if (Headers["transfer-encoding"].find("chunked") != std::string::npos)
			{
				while (true)
				{
					size_t LineEnd{};
					while ((LineEnd = Buffer.find("\r\n")) == std::string::npos)
					{
						if (!Fill())
						{
							return Fail(Where.Host + ": cut off");
						}
					}
					const size_t Size{ std::strtoull(Buffer.c_str(), nullptr, 16) };
					Buffer.erase(0, LineEnd + 2);
					if (Size == 0)
					{
						break;
					}
					while (Buffer.size() < Size + 2)
					{
						if (!Fill())
						{
							return Fail(Where.Host + ": cut off");
						}
					}
					if (!Sink(Buffer.data(), Size))
					{
						return Fail("stopped");
					}
					Buffer.erase(0, Size + 2);
				}
			}
			else
			{
				const bool Sized{ Headers.count("content-length") != 0 };
				uint64_t Left{ Sized ? std::strtoull(Headers["content-length"].c_str(), nullptr, 10) : UINT64_MAX };
				while (Left > 0)
				{
					if (Buffer.empty() && !Fill())
					{
						if (Sized)
						{
							return Fail(Where.Host + ": cut off");
						}
						break;
					}
					const size_t Take{ (size_t)std::min<uint64_t>(Left, Buffer.size()) };
					if (!Sink(Buffer.data(), Take))
					{
						return Fail("stopped");
					}
					Buffer.erase(0, Take);
					Left -= Sized ? Take : 0;
				}
			}
			pSsl.reset();
			CloseSocket(s);
			return true;
		}
		Error = "too many redirects";
		return false;
	}

	bool GetText(const std::string& Address, std::string& Out, std::string& Error)
	{
		return Get(Address, "application/vnd.github+json", [&Out](const char* p, size_t n)
			{
				Out.append(p, n);
				return Out.size() < (8u << 20);
			}, Error);
	}

	bool GetFile(const std::string& Address, const std::filesystem::path& To, UpdateProgress& P, std::string& Error)
	{
		std::ofstream File{ To, std::ios::binary | std::ios::trunc };
		if (!File)
		{
			Error = "can't write " + Utf8(To);
			return false;
		}
		const bool Ok{ Get(Address, "application/octet-stream", [&](const char* p, size_t n)
			{
				File.write(p, (std::streamsize)n);
				P.Done += n;
				return File.good() && !P.Cancel;
			}, Error) };
		if (P.Cancel)
		{
			Error = "Stopped, nothing was changed.";
		}
		return Ok && File.good();
	}

	//the settings file's own lines kept, as the f1 menu does (port/ui/settings/files.cpp)
	void SaveSetting(const std::string& Name, const std::string& Value)
	{
		std::vector<std::string> Lines;
		{
			std::ifstream File{ GE_SETTINGS_FILE };
			std::string Line;
			while (std::getline(File, Line))
			{
				Lines.push_back(Line);
			}
		}
		if (Lines.empty())
		{
			Lines.push_back("# written by the launcher, read when the game is started");
		}
		const std::string New{ Name + "=\"" + Value + "\"" };
		bool Found{};
		for (std::string& Line : Lines)
		{
			if (Line.rfind(Name + "=", 0) == 0)
			{
				Line = New;
				Found = true;
			}
		}
		if (!Found)
		{
			Lines.push_back(New);
		}
		std::ofstream File{ GE_SETTINGS_FILE, std::ios::binary | std::ios::trunc };
		for (const std::string& Line : Lines)
		{
			File << Line << '\n';
		}
	}

	bool Fetch(const std::string& Mine, const std::string& Skip, Release& Out)
	{
		std::string Text, Error;
		Json Root;
		if (!GetText(std::string(kApi) + "/releases/tags/" + kTag, Text, Error) || !JsonReader{ Text }.Read(Root))
		{
			std::fprintf(stderr, "update: no answer from GitHub: %s\n", Error.c_str());
			return false;
		}
		Out.Commit = Root["target_commitish"].Text;
		Out.Title = Root["name"].Text;
		for (const Json& Item : Root["assets"].Items)
		{
			const Asset A{ Item["name"].Text, Item["browser_download_url"].Text, std::strtoull(Item["size"].Text.c_str(), nullptr, 10) };
			if (A.Name == kPackage)
			{
				Out.Package = A;
			}
			else if (A.Name.rfind("Janus-Project-", 0) != 0 && A.Name.size() > 4 && A.Name.compare(A.Name.size() - 4, 4, ".zip") == 0)
			{
				Out.Modpacks.push_back(A);
			}
		}
		if (Out.Commit.size() < 7 || Out.Package.Url.empty())
		{
			std::fprintf(stderr, "update: the release has no %s\n", kPackage);
			return false;
		}
		if (Out.Commit.rfind(Mine, 0) == 0 || Mine.rfind(Out.Commit, 0) == 0 || Out.Commit == Skip)
		{
			std::fprintf(stderr, "update: %s (%.7s)\n", Out.Commit == Skip ? "skipped" : "up to date", Out.Commit.c_str());
			return false;
		}
		//what changed since, when GitHub can say (it can't for a commit it doesn't know)
		Text.clear();
		if (GetText(std::string(kApi) + "/compare/" + Mine + "..." + Out.Commit, Text, Error) && JsonReader{ Text }.Read(Root))
		{
			const std::vector<Json>& Commits{ Root["commits"].Items };
			for (auto It{ Commits.rbegin() }; It != Commits.rend() && Out.Changes.size() < kMostChanges; ++It)
			{
				const std::string& Message{ (*It)["commit"]["message"].Text };
				Out.Changes.push_back(Message.substr(0, Message.find('\n')));
			}
		}
		std::fprintf(stderr, "update: %.7s is out, this is %.7s\n", Out.Commit.c_str(), Mine.c_str());
		return true;
	}

	void SetEnv(const char* pName, const char* pValue)
	{
#ifdef _WIN32
		_putenv_s(pName, pValue != nullptr ? pValue : "");
#else
		pValue != nullptr ? setenv(pName, pValue, 1) : unsetenv(pName);
#endif
	}

	//every file under a folder, as paths relative to it with forward slashes
	std::vector<std::string> FilesIn(const std::filesystem::path& Folder)
	{
		std::vector<std::string> Files;
		std::error_code Ec;
		for (auto It{ std::filesystem::recursive_directory_iterator(Folder, Ec) }; !Ec && It != std::filesystem::recursive_directory_iterator(); It.increment(Ec))
		{
			if (It->is_regular_file(Ec))
			{
				Files.push_back(Utf8(std::filesystem::relative(It->path(), Folder, Ec).generic_u8string()));
			}
		}
		return Files;
	}
}

std::string ThisCommit()
{
#ifdef GE_LAUNCH_PORTABLE
	if (const char* pFrom{ std::getenv("GE_UPDATE_FROM") })
	{
		return pFrom; //as if it were that build, for trying the updater
	}
	return GE_COMMIT;
#else
	return ""; //a checkout updates with git
#endif
}

bool CheckForUpdate(const std::string& Skip, Release& Out)
{
	const std::string Mine{ ThisCommit() };
	if (Mine.empty() || std::getenv("GE_JUST_UPDATED") != nullptr)
	{
		return false;
	}
	//on its own thread, so a GitHub that doesn't answer doesn't hold the game back
	struct Shared
	{
		std::mutex Lock{};
		Release Found{};
		bool Ready{};
		bool Newer{};
	};
	const std::shared_ptr<Shared> pShared{ std::make_shared<Shared>() };
	std::thread([pShared, Mine, Skip]
		{
			Release Found;
			const bool Newer{ Fetch(Mine, Skip, Found) };
			std::lock_guard<std::mutex> Guard{ pShared->Lock };
			pShared->Found = std::move(Found);
			pShared->Newer = Newer;
			pShared->Ready = true;
		}).detach();
	const auto Until{ std::chrono::steady_clock::now() + std::chrono::seconds(kCheckSeconds) };
	while (std::chrono::steady_clock::now() < Until)
	{
		{
			std::lock_guard<std::mutex> Guard{ pShared->Lock };
			if (pShared->Ready)
			{
				Out = pShared->Found;
				return pShared->Newer;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	std::fprintf(stderr, "update: GitHub didn't answer in %d s, maybe next time\n", kCheckSeconds);
	return false;
}

std::vector<Asset> ChangedModpacks(const Release& R)
{
	std::vector<Asset> Changed;
	for (const Asset& A : R.Modpacks)
	{
		std::error_code Ec;
		const uintmax_t Size{ std::filesystem::file_size(FsPath("modpacks/" + A.Name), Ec) };
		if (Ec || Size != A.Size)
		{
			Changed.push_back(A);
		}
	}
	return Changed;
}

bool ApplyUpdate(const Release& R, bool Modpacks, UpdateProgress& P, std::string& Error)
{
	namespace fs = std::filesystem;
	const fs::path Work{ FsPath(kWork) }, Download{ Work / "download" }, New{ Work / "new" }, Old{ Work / "old" };
	std::error_code Ec;
	fs::remove_all(Download, Ec);
	fs::create_directories(Download, Ec);
	auto SetStep{ [&P](const std::string& Step)
		{
			std::lock_guard<std::mutex> Guard{ P.Lock };
			P.Step = Step;
		} };

	//everything downloaded first: stopping or failing here changes nothing
	const std::vector<Asset> Packs{ Modpacks ? ChangedModpacks(R) : std::vector<Asset>{} };
	uint64_t Total{ R.Package.Size };
	for (const Asset& A : Packs)
	{
		Total += A.Size;
	}
	P.Done = 0;
	P.Total = Total;
	SetStep(R.Package.Name);
	if (!GetFile(R.Package.Url, Download / R.Package.Name, P, Error))
	{
		return false;
	}
	for (const Asset& A : Packs)
	{
		SetStep(A.Name);
		if (!GetFile(A.Url, Download / FsPath(A.Name), P, Error))
		{
			return false;
		}
	}

	SetStep("Unpacking...");
	if (!ge::unzip_into(Utf8(Download / R.Package.Name), Utf8(New)))
	{
		Error = "The download couldn't be unpacked (see run.log).";
		return false;
	}
	std::vector<std::string> Files{ FilesIn(New) };
	std::erase_if(Files, PlayersOwn);
	if (std::find(Files.begin(), Files.end(), kExecutable) == Files.end())
	{
		Error = std::string("The download has no ") + kExecutable + ".";
		return false;
	}
	for (const Asset& A : Packs)
	{
		const std::string Name{ "modpacks/" + A.Name };
		fs::create_directories(New / "modpacks", Ec);
		fs::rename(Download / FsPath(A.Name), New / FsPath(Name), Ec);
		Files.push_back(Name);
	}

	//the old files moved aside into update/old, the new ones in: renaming works on a running
	//executable even on windows. a failure moves everything back
	SetStep("Putting the new files in place...");
	fs::remove_all(Old, Ec);
	std::vector<std::string> Moved, Placed;
	auto Undo{ [&]
		{
			std::error_code Ignored;
			for (const std::string& f : Placed)
			{
				fs::remove(FsPath(f), Ignored);
			}
			for (const std::string& f : Moved)
			{
				fs::rename(Old / FsPath(f), FsPath(f), Ignored);
			}
		} };
	for (const std::string& f : Files)
	{
		const fs::path Here{ FsPath(f) };
		if (fs::exists(Here, Ec))
		{
			fs::create_directories((Old / Here).parent_path(), Ec);
			fs::rename(Here, Old / Here, Ec);
			if (Ec)
			{
				Undo();
				Error = "Couldn't move " + f + " aside: " + Ec.message();
				return false;
			}
			Moved.push_back(f);
		}
		if (Here.has_parent_path())
		{
			fs::create_directories(Here.parent_path(), Ec);
		}
		fs::rename(New / Here, Here, Ec);
		if (Ec)
		{
			Undo();
			Error = "Couldn't put " + f + " in place: " + Ec.message();
			return false;
		}
		Placed.push_back(f);
	}
#ifndef _WIN32
	fs::permissions(FsPath(kExecutable), fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec
		| fs::perms::others_read | fs::perms::others_exec, Ec);
#endif
	fs::remove_all(Download, Ec);
	fs::remove_all(New, Ec);
	std::fprintf(stderr, "update: %zu file(s) replaced by %.7s's\n", Files.size(), R.Commit.c_str());
	return true;
}

void CleanUpdate()
{
#ifdef GE_LAUNCH_PORTABLE
	std::error_code Ec;
	std::filesystem::remove_all(FsPath(kWork), Ec);
#endif
}

void SkipRelease(const std::string& Commit)
{
	SaveSetting("LAUNCH_SKIPUPDATE", Commit);
}

void Restart(char** argv)
{
	std::fflush(stdout);
	std::fflush(stderr);
	//the executable in the game's folder (the launcher works in it), the new one now: this one's
	//own path (/proc/self/exe, SDL_GetBasePath) follows it to update/old. the new one doesn't ask
	//again, a pretended old commit would update forever
	std::error_code Ec;
	const std::filesystem::path Here{ std::filesystem::current_path(Ec) / kExecutable };
	SetEnv("GE_UPDATE_FROM", nullptr);
	SetEnv("GE_JUST_UPDATED", "1");
#ifdef _WIN32
	const std::wstring Path{ Here.wstring() };
	std::wstring Line{ GetCommandLineW() };
	STARTUPINFOW Start{};
	Start.cb = sizeof Start;
	PROCESS_INFORMATION Process{};
	if (CreateProcessW(Path.c_str(), Line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &Start, &Process))
	{
		CloseHandle(Process.hThread);
		CloseHandle(Process.hProcess);
	}
	(void)argv;
	std::exit(0);
#else
	const std::string Path{ Utf8(Here) };
	execv(Path.c_str(), argv);
	std::fprintf(stderr, "update: couldn't start %s, start it again yourself\n", Path.c_str());
	std::exit(1);
#endif
}
}
