#include <stdlib.h>
#include <iostream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <vector>

using boost::asio::ip::tcp;
using std::string;
using std::cout;
using std::endl;

class session
  : public std::enable_shared_from_this<session>
{
public:
  session(tcp::socket socket)
    : socket_(std::move(socket)){}

  void start()
  {
    do_read();
  }

private:
  tcp::socket socket_;
  enum { max_length = 1024 };
  char data_[max_length];
  struct env_
  {
    string REQUEST_METHOD;
    string REQUEST_URI;
    string QUERY_STRING;
    string SERVER_PROTOCOL;
    string HTTP_HOST;
  };
  struct env_ d;

  void parse(string data){
    std::vector<string> headers;
    //boost::split(headers, data, boost::is_any_of("\n"), boost::token_compress_on);
    size_t p;
    string line = data.substr(0, (p = data.find("\r\n")));
    data.erase(0, p+2);
    boost::split(headers, line, boost::is_any_of(" "), boost::token_compress_on);
    auto it = headers.begin();
    d.REQUEST_METHOD = *it;
    it++;
    size_t q = (*it).find('?');
    if(q != string::npos){
      d.REQUEST_URI = (*it).substr(1, q-1);
      d.QUERY_STRING = (*it).substr(q+1);
    }
    else{
      d.REQUEST_URI = (*it).substr(1);
      d.QUERY_STRING = "";
    }
    it++;
    d.SERVER_PROTOCOL = *it;
    p = data.find(' ')+1;
    d.HTTP_HOST = data.substr(p, data.find("\r\n")-p);
  }

  void do_read()
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            string data = data_;
            parse(data);
            do_write();
          }
        });
  }
  void set_cgi(){
    if(fork()==0){
      dup2(socket_.native_handle(),STDOUT_FILENO);
      dup2(socket_.native_handle(),STDIN_FILENO);
      string PATH = getenv("PATH");
      PATH += ":.";
      setenv("PATH", PATH.c_str(), 1);
      setenv("REQUEST_METHOD", d.REQUEST_METHOD.c_str(), 1);
      setenv("REQUEST_URI", d.REQUEST_URI.c_str(), 1);
      setenv("QUERY_STRING", d.QUERY_STRING.c_str(), 1);
      setenv("SERVER_PROTOCOL", d.SERVER_PROTOCOL.c_str(), 1);
      setenv("HTTP_HOST", d.HTTP_HOST.c_str(), 1);
      setenv("SERVER_ADDR", socket_.local_endpoint().address().to_string().c_str(), 1);
      setenv("SERVER_PORT", std::to_string(socket_.local_endpoint().port()).c_str(), 1);
      setenv("REMOTE_ADDR", socket_.remote_endpoint().address().to_string().c_str(), 1);
      setenv("REMOTE_PORT", std::to_string(socket_.remote_endpoint().port()).c_str(), 1);
      socket_.close();

      execlp(d.REQUEST_URI.c_str(), d.REQUEST_URI.c_str(), NULL);
      std::cerr << std::strerror(errno);
      exit(0);
    }
  }

  void do_write()
  {
    string status = "HTTP/1.1 200 OK \r\n";
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(status),
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec)
          {
            set_cgi();
            socket_.close();
          }
        });
  }
};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    do_accept();
  }

private:
  void do_accept()
  {
    acceptor_.async_accept(
        [this](boost::system::error_code ec, tcp::socket socket)
        {
          if (!ec)
          {
            auto s = std::make_shared<session>(std::move(socket));
            s->start();
          }
          do_accept();
        });
  }
  tcp::acceptor acceptor_;
};

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: async_tcp_echo_server <port>\n";
      return 1;
    }

    boost::asio::io_context io_context;
    server s(io_context, std::atoi(argv[1]));
    io_context.run();
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
