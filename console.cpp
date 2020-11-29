#include <cstdlib>
#include <iostream>
#include <string>
#include <fstream>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <boost/algorithm/string.hpp>
#include <vector>
#include <set>

using boost::asio::ip::tcp;
using std::cout;
using std::endl;
using std::string;

class session
    : public std::enable_shared_from_this<session>
{
public:
  session(boost::asio::io_context &ioc,
          string &host, string &port, string &doc)
      : socket_(ioc), resolver_(ioc)
  {
    file.open(doc);
    auto self = shared_from_this();
    resolver_.async_resolve(tcp::v4(), host, port, std::bind(&session::on_resolve, this, std::placeholders::_1, std::placeholders::_2));
  }

private:
  string host, response;
  std::ifstream file;
  short port;
  tcp::socket socket_;
  tcp::resolver resolver_;
  enum{ max_length = 1024 };
  char data_[max_length];

  void on_resolve(boost::system::error_code ec, tcp::resolver::results_type endpoint)
  {
    if (!ec)
    {
      boost::asio::async_connect(socket_, endpoint,
                                 [this](boost::system::error_code ec, tcp::endpoint endpoint) {
                                   if (!ec)
                                   {
                                     do_read();
                                   }
                                 });
    }
  }
  void to_client()
  {
    cout << response << std::flush;
    response.clear();
  }

  void do_read()
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
                            [this, self](boost::system::error_code ec, std::size_t length) {
                              if (!ec)
                              {
                                response += data_;
                                to_client();
                                if (response.find("%") != string::npos)
                                  do_write();
                                else
                                  do_read();
                              }
                            });
  }

  void do_write()
  {
    string cmd;
    getline(file, cmd);
    response = cmd;
    to_client();
    auto self(shared_from_this());
    boost::asio::async_write(socket_, boost::asio::buffer(cmd),
                             [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                               if (!ec)
                               {
                                 do_read();
                               }
                             });
  }
};

class session_manager
{
public:
  session_manager(boost::asio::io_context &io_context) : io_context_(io_context) {}

  void make_session()
  {
    string query = getenv("QUERY_STRING");
    std::vector<string> params;
    boost::split(params, query, boost::is_any_of("&"), boost::token_compress_on);
    for (auto it = params.begin(); it != params.end(); it += 3)
    {
      if (!boost::ends_with(*it, "="))
      {
        string host = (*it).substr((*it).find('='));
        string port = (*(it + 1)).substr((*(it + 1)).find('='));
        string doc = (*(it + 1)).substr((*(it + 1)).find('='));

        auto s = std::make_shared<session>(io_context_, host, port, doc);
        sessions.insert(s);
      }
      else
        break;
    }
  }

private:
  boost::asio::io_context &io_context_;
  std::set<std::shared_ptr<session>> sessions;
};

int main(int argc, char *argv[])
{

  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: async_tcp_echo_server <port>\n";
      return 1;
    }

    boost::asio::io_context io_context;
    session_manager cntl(io_context);
    cntl.make_session();
    io_context.run();
  }
  catch (std::exception &e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
