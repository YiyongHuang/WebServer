#include "http_request.h"

using namespace std;

std::unordered_map<std::string, std::string> HttpRequest::mime_;

void HttpRequest::httpRequestInit()
{
    mime_[".html"] = "text/html";
    mime_[".avi"] = "video/x-msvideo";
    mime_[".bmp"] = "image/bmp";
    mime_[".c"] = "text/plain";
    mime_[".doc"] = "application/msword";
    mime_[".gif"] = "image/gif";
    mime_[".gz"] = "application/x-gzip";
    mime_[".htm"] = "text/html";
    mime_[".ico"] = "application/x-ico";
    mime_[".jpg"] = "image/jpeg";
    mime_[".png"] = "image/png";
    mime_[".txt"] = "text/plain";
    mime_[".mp3"] = "audio/mp3";
    mime_["default"] = "text/html";
}

HttpRequest::HttpRequest(int listen_fd) : fd_(listen_fd)
{
    keep_alive_ = false;
    readTimes_ = 0;
}

URIState HttpRequest::parseURI()
{
    string &str = inBuffer_;
    // std::cout << inBuffer_;
    size_t pos = str.find('\r', 0);
    // 未读到请求行，需重新读取；
    if (pos < 0)
    {
        return PARSE_URI_AGAIN;
    }
    string request_uri = str.substr(0, pos);
    if (str.size() > pos + 2)
        str = str.substr(pos + 2);
    else
        str.clear();

    int posGET = request_uri.find("GET");
    int posPOST = request_uri.find("POST");
    int posHEAD = request_uri.find("HEAD");
    if (posGET >= 0)
    {
        pos = posGET;
        method_ = METHOD_GET;
    }
    else if (posPOST >= 0)
    {
        pos = posPOST;
        method_ = METHOD_POST;
    }
    else if (posHEAD >= 0)
    {
        pos = posHEAD;
        method_ = METHOD_HEAD;
    }
    // filename
    pos = request_uri.find('/', pos);
    if (pos < 0)
        return PARSE_URI_ERROR;
    else
    {
        size_t pos_ = request_uri.find(' ', pos);
        if (pos_ < 0)
            return PARSE_URI_ERROR;
        else
        {
            if (pos_ - pos > 1)
            {
                fileName_ = request_uri.substr(pos + 1, pos_ - pos - 1);
                pos = pos_;
                size_t pos__ = fileName_.find('?');
                if (pos__ >= 0)
                    fileName_ = fileName_.substr(0, pos__);
            }
            else
                fileName_ = "index.html";
        }
        pos = pos_;
    }
    // http_version
    pos = request_uri.find('/', pos);
    if (pos < 0)
        return PARSE_URI_ERROR;
    if (request_uri.size() - pos <= 3)
        return PARSE_URI_ERROR;
    if (request_uri.substr(pos + 1, 3) == "1.0")
        HttpVersion_ = HTTP_10;
    else if (request_uri.substr(pos + 1, 3) == "1.1")
        HttpVersion_ = HTTP_11;
    else if (request_uri.substr(pos + 1, 3) == "2.0")
        HttpVersion_ = HTTP_20;
    else
        return PARSE_URI_ERROR;
    return PARSE_URI_SUCCESS;
}

HeaderState HttpRequest::parseHeader()
{
    string &str = inBuffer_;
    ParseState headState = H_START;
    int key_start = -1, key_end = -1, val_start = -1, val_end = -1;
    bool finished = false;
    size_t i = 0;
    for (; i < str.size() && !finished; ++i)
    {
        switch (headState)
        {
        case H_START:
        {
            if (str[i] == '\r' || str[i] == '\n')
                break;
            key_start = i;
            headState = H_KEY;
            break;
        }
        case H_KEY:
        {
            if (str[i] == ':')
            {
                key_end = i;
                if (key_end - key_start <= 0)
                    return PARSE_HEADER_ERROR;
                headState = H_COLON;
            }
            else if (str[i] == '\n' || str[i] == '\r')
                return PARSE_HEADER_ERROR;
            break;
        }
        case H_COLON:
        {
            if (str[i] == ' ')
                headState = H_SPACES_AFTER_COLON;
            else
                return PARSE_HEADER_ERROR;
            break;
        }
        case H_SPACES_AFTER_COLON:
        {
            headState = H_VALUE;
            val_start = i;
            break;
        }
        case H_VALUE:
        {
            if (str[i] == '\r')
            {
                val_end = i;
                headState = H_CR;
                if (val_end - val_start <= 0)
                    return PARSE_HEADER_ERROR;
            }
            else if (i - val_start > 255)
                return PARSE_HEADER_ERROR;
            break;
        }
        case H_CR:
        {
            if (str[i] == '\n')
            {
                headState = H_LF;
                string key(str.begin() + key_start, str.begin() + key_end);
                string value(str.begin() + val_start, str.begin() + val_end);
                headers_[key] = value;
            }
            else
                return PARSE_HEADER_ERROR;
            break;
        }
        case H_LF:
        {
            if (str[i] == '\r')
            {
                headState = H_END_CR;
            }
            else
            {
                key_start = i;
                headState = H_KEY;
            }
            break;
        }
        case H_END_CR:
        {
            if (str[i] == '\n')
                headState = H_END_LF;
            else
                return PARSE_HEADER_ERROR;
            break;
        }
        case H_END_LF:
        {
            finished = true;
            break;
        }
        }
    }
    if (headState == H_END_LF)
    {
        str = str.substr(i);
        if (headers_.find("Connection") != headers_.end() && headers_["Connection"] == "keep-alive")
            keep_alive_ = true;
        return PARSE_HEADER_SUCCESS;
    }
    return PARSE_HEADER_AGAIN;
}

ResponseState HttpRequest::responseRequest()
{
    // GET、POST、HEAD请求暂时都按GET简单处理
    // if (method_ == METHOD_POST)
    // {
    // }
    if (method_ == METHOD_GET || method_ == METHOD_HEAD || method_ == METHOD_POST)
    {
        string header;
        header += "HTTP/1.1 200 OK\r\n";
        // char header[MAX_BUFF];
        // sprintf(header, "HTTP/1.1 %d OK\r\n", 200);
        if (headers_.find("Connection") != headers_.end() && headers_["Connection"] == "keep-alive")
        {
            keep_alive_ = true;
            header += "Connection: keep-alive\r\n";
            header += "Keep-Alive: timeout=" + to_string(TIMER_TIMEOUT) + "\r\n";
        }
        if (method_ == METHOD_HEAD)
        {
            outBuffer_ += header + "\r\n";
            if (writen(fd_, outBuffer_) < 0)
            {
                perror("Send head failed");
                return RESPONSE_ERROR;
            }
            return RESPONSE_SUCCESS;
        }
        int dot_pos = fileName_.find('.');
        string filetype;
        if (dot_pos < 0)
            filetype = mime_["default"];
        else
            filetype = mime_[fileName_.substr(dot_pos)];
        // sprintf(header, "%sContent-type: %s", header, fileType);
        header += "Content-Type: " + filetype + "\r\n";
        struct stat statbuff;
        if (stat(fileName_.c_str(), &statbuff) < 0)
        {
            header.clear();
            handleError(404, "Not Found!");
            return RESPONSE_ERROR;
        }
        // sprintf(header, "%sContent-length: %lld\r\n", header, statbuff.st_size);
        // sprintf(header, "%s\r\n", header);
        header += "Content-Length: " + to_string(statbuff.st_size) + "\r\n";
        header += "\r\n";
        outBuffer_ += header;
        // ssize_t send_len;
        // if (send_len != strlen(header))
        // {
        //     perror("Send header failed");
        //     return RESPONSE_ERROR;
        // }
        int file_fd = open(fileName_.c_str(), O_RDONLY, 0);
        char *file_addr = static_cast<char *>(mmap(NULL, statbuff.st_size, PROT_READ, MAP_PRIVATE, file_fd, 0));
        close(file_fd);
        // send_len = writen(fd_, file_addr, statbuff.st_size);
        if (file_addr == (char *)-1)
        {
            outBuffer_.clear();
            munmap(file_addr, statbuff.st_size);
            handleError(404, "Not Found!");
            perror("mmap error");
            return RESPONSE_ERROR;
        }
        outBuffer_ += string(file_addr, file_addr + statbuff.st_size);
        if (writen(fd_, outBuffer_) < 0)
        {
            perror("Send file failed");
            return RESPONSE_ERROR;
        }
        munmap(file_addr, statbuff.st_size);
        return RESPONSE_SUCCESS;
    }
    return RESPONSE_ERROR;
}

void HttpRequest::handleError(int error_no, std::string msg)
{
    msg = " " + msg;
    // char send_buff[4096];
    string body_buff, header_buff;
    body_buff += "<html><title>Error</title>";
    body_buff += "<body bgcolor=\"ffffff\">";
    body_buff += to_string(error_no) + msg;
    body_buff += "<hr></body></html>";

    header_buff += "HTTP/1.1 " + to_string(error_no) + msg + "\r\n";
    header_buff += "Content-type: text/html\r\n";
    header_buff += "Connection: close\r\n";
    header_buff += "Content-length: " + to_string(body_buff.size()) + "\r\n";
    header_buff += "\r\n";
    // sprintf(send_buff, "%s", header_buff.c_str());
    // writen(fd_, send_buff, strlen(send_buff));
    // sprintf(send_buff, "%s", body_buff.c_str());
    // writen(fd_, send_buff, strlen(send_buff));
    writen(fd_, header_buff);
    writen(fd_, body_buff);
}

// 保持长连接时只需要每次将请求重置，再用于接收下一个请求，避免了创建和销毁的开销
void HttpRequest::resetRequest()
{
    inBuffer_.clear();
    outBuffer_.clear();
    keep_alive_ = false;
    fileName_.clear();
    headers_.clear();
    readTimes_ = 0;
    if (timer_.lock())
    {
        std::shared_ptr<TimerNode> tmp(timer_.lock());
        tmp->seperateRequest();
        timer_.reset();
    }
}

void HttpRequest::handleRequest()
{
    ResponseState responseState = RESPONSE_ERROR;
    do
    {
        int read_num = readn(fd_, inBuffer_);
        if (read_num < 0)
        {
            perror("read error");
            // handleError(400, "Bad Request");
            break;
        }
        else if (read_num == 0)
        {
            perror("read empty");
            if (errno == EAGAIN)
            {
                if (readTimes_ > MAX_READ_TIMES)
                    return;
                else
                {
                    ++readTimes_;
                    break;
                }
            }
            else
                return;
        }

        // parse URI
        URIState uriState = this->parseURI();
        if (uriState == PARSE_URI_AGAIN)
            break;
        else if (uriState == PARSE_URI_ERROR)
        {
            perror("parse uri error");
            // handleError(400, "Bad Request");
            return;
        }
        // parse header
        HeaderState headerState = this->parseHeader();
        if (headerState == PARSE_HEADER_AGAIN)
            break;
        else if (headerState == PARSE_HEADER_ERROR)
        {
            perror("parse header error");
            // handleError(400, "Bad Request");
            return;
        }
        // response
        if (method_ == METHOD_GET || method_ == METHOD_HEAD || method_ == METHOD_POST)
        {
            responseState = this->responseRequest();
            if (responseState == RESPONSE_ERROR)
            {
                perror("response error");
                // handleError(500, "Internal Server Error");
                return;
            }
            else
                break;
        }
        else
            break;
    } while (true);
    if (responseState == RESPONSE_SUCCESS)
    {
        if (keep_alive_)
        {
            this->resetRequest();
        }
        else
            return;
    }
    // 给该请求重新添加计时器
    // std::cout<<"add new timer"<<std::endl;
    TimerManager::addTimer(shared_from_this(), TIMER_TIMEOUT);
    Epoll::epollMod(fd_, shared_from_this(), (EPOLLIN | EPOLLET | EPOLLONESHOT));
}

void HttpRequest::sepereteTimer()
{
    if (timer_.lock())
    {
        // std::cout<<"seperate timer"<<std::endl;
        std::shared_ptr<TimerNode> tmp(timer_.lock());
        tmp->seperateRequest();
        timer_.reset();
    }
}

HttpRequest::~HttpRequest()
{
    // std::cout << "~request" << std::endl;
    // Epoll::epollDel(fd_, ((EPOLLIN | EPOLLET | EPOLLONESHOT)));
    close(fd_);
}
