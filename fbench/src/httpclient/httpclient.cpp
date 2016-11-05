// Copyright 2016 Yahoo Inc. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.
#include "httpclient.h"

#define FETCH_BUFLEN 5120
#define FIXED_REQ_MAX 256


HTTPClient::ConnCloseReader
HTTPClient::ConnCloseReader::_instance;

HTTPClient::ContentLengthReader
HTTPClient::ContentLengthReader::_instance;

HTTPClient::ChunkedReader
HTTPClient::ChunkedReader::_instance;


HTTPClient::HTTPClient(const char *hostname, int port,
                       bool keepAlive, bool headerBenchmarkdataCoverage,
                       const std::string & extraHeaders, const std::string &authority)
    : _socket(new FastOS_Socket()),
      _hostname(hostname),
      _port(port),
      _keepAlive(keepAlive),
      _headerBenchmarkdataCoverage(headerBenchmarkdataCoverage),
      _extraHeaders(extraHeaders),
      _authority(authority),
    _reuseCount(0),
    _bufsize(10240),
    _buf(new char[_bufsize]),
    _bufused(0),
    _bufpos(0),
    _headerinfo(),
    _isOpen(false),
    _httpVersion(0),
    _requestStatus(0),
    _totalHitCount(-1),
    _connectionCloseGiven(false),
    _contentLengthGiven(false),
    _chunkedEncodingGiven(false),
    _keepAliveGiven(false),
    _contentLength(0),
    _chunkSeq(0),
    _chunkLeft(0),
    _dataRead(0),
    _dataDone(false),
    _reader(NULL)
{
    _socket->SetAddressByHostName(port, hostname);
    if (_authority == "") {
        char tmp[1024];
        snprintf(tmp, 1024, "%s:%d", hostname, port);
        _authority = tmp;
    }
}

ssize_t
HTTPClient::FillBuffer() {
    _bufused = _socket->Read(_buf, _bufsize); // may be -1
    _bufpos  = 0;
    return _bufused;
}

HTTPClient::~HTTPClient()
{
    if (_socket)
        _socket->Close();
    delete [] _buf;
}

ssize_t
HTTPClient::ReadLine(char *buf, size_t bufsize)
{
    size_t len   = 0;
    int    lastC = 0;
    int    c     = ReadByte();

    if (c == -1)
        return -1;
    while (c != '\n' && c != -1) {
        if (len + 1 < bufsize)
            buf[len] = c;
        len++;
        lastC = c;
        c = ReadByte();
    }
    if (lastC == '\r')
        len--;
    if (len < bufsize)
        buf[len] = '\0';           // terminate string
    else if (bufsize > 0)
        buf[bufsize - 1] = '\0';   // terminate string
    return len;
}

bool
HTTPClient::Connect(const char *url)
{
    char tmp[4096];
    char *req = NULL;
    uint32_t req_max = 0;
    uint32_t url_len = strlen(url);
    uint32_t host_len = _hostname.size();

    // Add additional headers
    std::string headers = _extraHeaders;

    // this is always requested to get robust info on total hit count.
    headers += "X-Yahoo-Vespa-Benchmarkdata: true\r\n";

    if ( _headerBenchmarkdataCoverage ) {
        headers += "X-Yahoo-Vespa-Benchmarkdata-Coverage: true\r\n";
    }

    if (url_len + host_len + headers.length() + FIXED_REQ_MAX < sizeof(tmp)) {
        req = tmp;
        req_max = sizeof(tmp);
    } else {
        req_max = url_len + host_len + headers.length() + FIXED_REQ_MAX;
        req = new char[req_max];
        assert(req != NULL);
    }

    if (headers.length() > 0) {
        headers += "\r\n";
    }
    // create request
    if(_keepAlive) {
        snprintf(req, req_max,
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "User-Agent: fbench/4.2.10\r\n"
                 "%s"
                 "\r\n",
                 url, _authority.c_str(), headers.c_str());
    } else {
        snprintf(req, req_max,
                 "GET %s HTTP/1.1\r\n"
                 "Host: %s\r\n"
                 "Connection: close\r\n"
                 "User-Agent: fbench/4.2.10\r\n"
                 "%s"
                 "\r\n",
                 url, _authority.c_str(), headers.c_str());
    }

    // try to reuse connection if keep-alive is enabled
    if (_keepAlive
        && _socket->IsOpened()
        && _socket->Write(req, strlen(req)) == (ssize_t)strlen(req)
        && FillBuffer() > 0) {

        // DEBUG
        // printf("Socket Connection reused!\n");
        _reuseCount++;
        if (req != tmp) {
            delete [] req;
        }
        return true;
    } else {
        _socket->Close();
        ResetBuffer();
    }

    // try to open new connection to server
    if (_socket->SetSoBlocking(true)
        && _socket->Connect()
        && _socket->SetNoDelay(true)
        && _socket->SetSoLinger(false, 0)
        && _socket->Write(req, strlen(req)) == (ssize_t)strlen(req)) {

        // DEBUG
        // printf("New Socket connection!\n");
        if (req != tmp) {
            delete [] req;
        }
        return true;
    } else {
        _socket->Close();
    }

    // DEBUG
    // printf("Connect FAILED!\n");
    if (req != tmp) {
        delete [] req;
    }
    return false;
}

char *
HTTPClient::SplitString(char *input, int &argc, char **argv, int maxargs)
{
    for (argc = 0, argv[0] = input; *input != '\0'; input++)
        if (*input == '\t' || *input == ' ') {
            *input = '\0';
            if (*(argv[argc]) != '\0' && ++argc >= maxargs)
                return (input + 1);       // INCOMPLETE
            argv[argc] = (input + 1);
        }
    if (*(argv[argc]) != '\0')
        argc++;
    return NULL;                    // COMPLETE
}

bool
HTTPClient::ReadHTTPHeader()
{
    int     lineLen;
    char    line[4096];
    int     argc;
    char   *argv[32];
    int     i;

    // clear HTTP header flags
    _connectionCloseGiven = false;
    _contentLengthGiven   = false;
    _chunkedEncodingGiven = false;
    _keepAliveGiven       = false;

    // read and split status line
    if ((lineLen = ReadLine(line, 4096)) <= 0)
        return false;
    SplitString(line, argc, argv, 32);

    // parse status line
    if (argc >= 2) {
        if (strncmp(argv[0], "HTTP/", 5) != 0)
            return false;
        _httpVersion = (strncmp(argv[0], "HTTP/1.0", 8) == 0) ?
                       0 : 1;
        _requestStatus = atoi(argv[1]);
    } else {
        return false;
    }

    // DEBUG
    // printf("HTTP: version: 1.%d\n", _httpVersion);
    // printf("HTTP: status: %d\n", _requestStatus);

    // read and parse rest of header
    while((lineLen = ReadLine(line, 4096)) > 0) {

        // DEBUG
        // printf("HTTP-Header: '%s'\n", line);

        if (strncmp(line, "X-Yahoo-Vespa-", strlen("X-Yahoo-Vespa")) == 0) {
            const auto benchmark_data = std::string(line + 14);

            auto strpos = benchmark_data.find("TotalHitCount:");
            if (strpos != std::string::npos) {
                _totalHitCount = atoi(benchmark_data.substr(14).c_str());
            }

            // Make sure to have enough memory in _headerinfo
            _headerinfo += benchmark_data;
            _headerinfo += "\n";
        }

        SplitString(line, argc, argv, 32);
        if (argc > 1) {
            if (strcasecmp(argv[0], "connection:") == 0) {
                for(i = 1; i < argc; i++) {
                    // DEBUG
                    // printf("HTTP: Connection: '%s'\n", argv[i]);

                    if (strcasecmp(argv[i], "keep-alive") == 0) {
                        _keepAliveGiven = true;

                        // DEBUG
                        // printf("HTTP: connection keep-alive given\n");
                    }
                    if (strcasecmp(argv[i], "close") == 0) {
                        _connectionCloseGiven = true;

                        // DEBUG
                        // printf("HTTP: connection close given\n");
                    }
                }
            }
            if (strcasecmp(argv[0], "content-length:") == 0) {
                _contentLengthGiven = true;
                _contentLength = atoi(argv[1]);

                // DEBUG
                // printf("HTTP: content length : %d\n", _contentLength);       
            }
            if (strcasecmp(argv[0], "transfer-encoding:") == 0
                && strcasecmp(argv[1], "chunked") == 0) {
                _chunkedEncodingGiven = true;

                // DEBUG
                // printf("HTTP: chunked encoding given\n");
            }
        }
    }
    return (lineLen == 0);
}

bool
HTTPClient::ReadChunkHeader()
{
    int  lineLen;
    char numStr[10];
    char c;
    int  i;

    if (_chunkSeq++ > 0 && ReadLine(NULL, 0) != 0)
        return false;                  // no CRLF(/LF) after data block

    assert(_chunkLeft == 0);
    if (ReadLine(numStr, 10) <= 0)
        return false;                  // chunk length not found
    for (i = 0; i < 10; i++) {
        c = numStr[i];
        if (c >= 'a' && c <= 'f')
            c = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            c = c - 'A' + 10;
        else if (c >= '0' && c <= '9')
            c = c - '0';
        else
            break;
        if (i >= 8)                    // can't handle chunks this big
            return false;
        _chunkLeft = (_chunkLeft << 4) + c;
    }

    // DEBUG
    // printf("CHUNK: Length: %d\n", _chunkLeft);

    if (_chunkLeft == 0) {
        while ((lineLen = ReadLine(NULL, 0)) > 0);   // skip trailer
        if (lineLen < 0)
            return false;                              // data error
        _dataDone = true;                            // got last chunk
    }
    return true;
}

bool
HTTPClient::Open(const char *url)
{
    if (_isOpen)
        Close();

    ResetBuffer();
    _dataRead  = 0;
    _dataDone  = false;
    _isOpen    = Connect(url);
    if(!_isOpen || !ReadHTTPHeader()) {
        Close();
        return false;
    }
    if(_chunkedEncodingGiven) {
        _chunkSeq  = 0;
        _chunkLeft = 0;

        // DEBUG
        // printf("READER = Chunked\n");
        _reader = ChunkedReader::GetInstance();
    } else if(_contentLengthGiven) {

        // DEBUG
        // printf("READER = ContentLength\n");
        _reader = ContentLengthReader::GetInstance();
    } else {

        // DEBUG
        // printf("READER = ConnClose\n");
        _reader = ConnCloseReader::GetInstance();
    }
    return true;
}

ssize_t
HTTPClient::ConnCloseReader::Read(HTTPClient &client,
                                  void *buf, size_t len)
{
    size_t  fromBuffer = 0;
    ssize_t res        = 0;
    ssize_t readRes;

    if (client._bufused > client._bufpos) { // data in buffer ?
        fromBuffer = (((size_t)(client._bufused - client._bufpos)) > len) ?
                     len : client._bufused - client._bufpos;
        memcpy(buf, client._buf + client._bufpos, fromBuffer);
        client._bufpos += fromBuffer;
        client._dataRead += fromBuffer;
        res = fromBuffer;
    }
    if ((len - fromBuffer) > (len >> 1)) {
        readRes = client._socket->Read(static_cast<char *>(buf)
                                       + fromBuffer, len - fromBuffer);
        if (readRes < 0) {
            client.Close();
            return -1;
        }
        if (readRes == 0)
            client._dataDone = true;
        client._dataRead += readRes;
        res += readRes;
    }
    return res;
}

ssize_t
HTTPClient::ContentLengthReader::Read(HTTPClient &client,
                                      void *buf, size_t len)
{
    size_t  fromBuffer = 0;
    ssize_t res        = 0;
    ssize_t readLen;
    ssize_t readRes;

    if (client._bufused > client._bufpos) { // data in buffer ?
        fromBuffer = (((size_t)(client._bufused - client._bufpos)) > len) ?
                     len : client._bufused - client._bufpos;
        memcpy(buf, client._buf + client._bufpos, fromBuffer);
        client._bufpos += fromBuffer;
        client._dataRead += fromBuffer;
        res = fromBuffer;
        if (client._dataRead >= client._contentLength) {
            client._dataDone = true;
            return res;
        }
    }
    if ((len - fromBuffer) > (len >> 1)) {
        readLen = (len - fromBuffer
                   < client._contentLength - client._dataRead) ?
                  len - fromBuffer : client._contentLength - client._dataRead;
        readRes = client._socket->Read(static_cast<char *>(buf)
                                       + fromBuffer, readLen);
        if (readRes < 0) {
            client.Close();
            return -1;
        }
        client._dataRead += readRes;
        res += readRes;
        if (client._dataRead >= client._contentLength) {
            client._dataDone = true;
            return res;
        }
        if (readRes == 0) {     // data lost because server closed connection
            client.Close();
            return -1;
        }
    }
    return res;
}

ssize_t
HTTPClient::ChunkedReader::Read(HTTPClient &client,
                                void *buf, size_t len)
{
    size_t  fromBuffer = 0;
    ssize_t res        = 0;

    while ((len - res) > (len >> 1)) {
        if (client._chunkLeft == 0) {
            if (!client.ReadChunkHeader()) {
                client.Close();
                return -1;
            }
            if (client._dataDone)
                return res;
        }
        if (client._bufused == client._bufpos) {
            if (client.FillBuffer() <= 0) {
                client.Close();
                return -1;
            }
        }
        fromBuffer = ((len - res) < ((size_t)(client._bufused - client._bufpos))) ?
                     len - res : client._bufused - client._bufpos;
        fromBuffer = (client._chunkLeft < fromBuffer) ?
                     client._chunkLeft : fromBuffer;
        memcpy(static_cast<char *>(buf) + res, client._buf + client._bufpos, fromBuffer);
        client._bufpos += fromBuffer;
        client._dataRead += fromBuffer;
        client._chunkLeft -= fromBuffer;
        res += fromBuffer;
    }
    return res;
}

ssize_t
HTTPClient::Read(void *buf, size_t len)
{
    if (!_isOpen)
        return -1;
    if (_dataDone)
        return 0;
    return _reader->Read(*this, buf, len);
}

bool
HTTPClient::Close()
{
    if (!_isOpen)
        return true;

    _isOpen = false;
    return (!_keepAlive
            || _connectionCloseGiven
            || !_dataDone
            || (_httpVersion == 0 && !_keepAliveGiven)) ?
        _socket->Close() : true;
}

HTTPClient::FetchStatus
HTTPClient::Fetch(const char *url, std::ostream *file)
{
    size_t  buflen   = FETCH_BUFLEN;
    char    buf[FETCH_BUFLEN];      // NB: ensure big enough thread stack.
    ssize_t readRes  = 0;
    ssize_t written  = 0;

    if (!Open(url)) {
        return FetchStatus(false, _requestStatus, _totalHitCount, 0);
    }

    // Write headerinfo
    if (file) {
        file->write(_headerinfo.c_str(), _headerinfo.length());
        if (file->fail()) {
            Close();
            return FetchStatus(false, _requestStatus, _totalHitCount, 0);
        }
        file->write("\r\n", 2);
        // Reset header data.
        _headerinfo = "";
    }

    while((readRes = Read(buf, buflen)) > 0) {
        if(file != NULL) {
            if (!file->write(buf, readRes)) {
                Close();
                return FetchStatus(false, _requestStatus, _totalHitCount, written);
            }
        }
        written += readRes;
    }
    Close();

    return FetchStatus(_requestStatus == 200 && readRes == 0 && _totalHitCount >= 0,
                       _requestStatus,
                       _totalHitCount,
                       written);
}