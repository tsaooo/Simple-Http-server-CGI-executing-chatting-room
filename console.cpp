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
struct info{
    string host;
    string port;
    string doc;
};

class session
    : public std::enable_shared_from_this<session>
{
public:
  string host_, port_, id_;

  session(boost::asio::io_context &ioc,
          string &host, string &port, string &doc, string &id)
      : host_(host), port_(port), id_(id), doc_(doc), socket_(ioc), resolver_(ioc)
  {}
  void start(){
    string path = "./test_case/" + doc_;
    file.open(path);
    auto self = shared_from_this();
    resolver_.async_resolve(tcp::v4(), host_, port_, std::bind(&session::on_resolve, this, std::placeholders::_1, std::placeholders::_2));
  }

private:
  string response, doc_;
  std::ifstream file;
  tcp::socket socket_;
  tcp::resolver resolver_;
  enum{ max_length = 1024};
  char data_[max_length];

  void escape(string &s){
    boost::replace_all(s, "&", "&amp;");
    boost::replace_all(s, ">", "&gt;");
    boost::replace_all(s, "<", "&lt;");
    boost::replace_all(s, "\n", "&NewLine;");
  }
  void on_resolve(boost::system::error_code ec, tcp::resolver::results_type endpoints)
  {
    if (!ec)
    {
      std::cerr << "on_resolve\n";
      auto self = shared_from_this();
      boost::asio::async_connect(socket_, endpoints,
                                 [this, self](boost::system::error_code ec, tcp::endpoint endpoint) {
                                   if (!ec)
                                   {
                                     std::cerr << "on_connect\n";
                                     do_read();
                                   }
                                   else
                                   {
                                     std::cerr << ec.message() << endl;
                                   }
                                   
                                 });
    }
  }
  void to_client_cmd()
  {
    std::cerr <<"to client_cmd:\n";
    std::cerr << response << endl;
    char str[100];
    escape(response);
    sprintf(str, "<script>document.getElementById('%s').innerHTML += '<b>%s</b>';</script>", id_.c_str(), response.c_str());
    string s = str;
    cout << s << std::flush;
    response.clear();
  }
  void to_client_res(){
    std::cerr <<"to client_res:\n";
    std::cerr << response << endl;
    char str[100];
    escape(response);
    sprintf(str, "<script>document.getElementById('%s').innerHTML += '%s';</script>", id_.c_str(), response.c_str());
    string s = str;
    cout << s << std::flush;
    response.clear();
  }

  void do_read()
  {
    auto self(shared_from_this());
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
                            [this, self](boost::system::error_code ec, std::size_t length) {
                              std::cerr << "on_read\n";
                              if (!ec)
                              {
                                std::cerr << "on_read\n";
                                response += data_;
                                to_client_res();
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
    if(getline(file, cmd))
    {
      response = cmd;
      to_client_cmd();
      auto self(shared_from_this());
      boost::asio::async_write(socket_, boost::asio::buffer(cmd),
                              [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                                std::cerr << "on_w\n";
                                if (!ec)
                                {
                                  std::cerr << "on_w\n";
                                  do_read();
                                }
                              });
    }
    else
      socket_.close();
  }
};

class session_manager
{
public:
  session_manager(boost::asio::io_context &io_context) : io_context_(io_context) {}
  std::vector <std::shared_ptr<session>> sessions;

  void parse_query()
  {
    string query = getenv("QUERY_STRING");
    std::vector<string> params;
    boost::split(params, query, boost::is_any_of("&"), boost::token_compress_on);

    for (auto it = params.begin(); it != params.end(); it += 3)
    {
      if (!boost::ends_with(*it, "="))
      {
        string host = (*it).substr((*it).find('=')+1);
        string port = (*(it + 1)).substr((*(it + 1)).find('=')+1);
        string doc = (*(it + 2)).substr((*(it + 1)).find('=')+1);
        string id = "s" + (*it).substr(1,1);
        std::cerr << host << " " << port << " " << doc << " " << id << endl;

        auto s = std::make_shared<session>(io_context_, host, port, doc, id);
        sessions.push_back(s);
      }
    }
  }
  void start(){
    for(const auto &s: sessions){
      s->start();
    }
  }
private:
  boost::asio::io_context &io_context_;
};

void prt_html(std::vector <std::shared_ptr<session>> &sessions){
  string thead, tbody;
  for(const auto& s:sessions){
    char str1[100];
    char str2[100];

    sprintf(str1, "<th scope=\"col\">%s:%s</th>\n", s->host_.c_str(), s->port_.c_str());
    thead += str1;
    sprintf(str2, "<td><pre id=\"%s\" class=\"mb-0\"></pre></td>\n", s->id_.c_str());
    tbody += str2;
  }
  string h = R"(
    <!DOCTYPE html>
    <html lang="en">
      <head>
        <meta charset="UTF-8" />
        <title>NP Project 3 Sample Console</title>
        <link
          rel="stylesheet"
          href="https://cdn.jsdelivr.net/npm/bootstrap@4.5.3/dist/css/bootstrap.min.css"
          integrity="sha384-TX8t27EcRE3e/ihU7zmQxVncDAy5uIKz4rEkgIXeMed4M0jlfIDPvg6uqKI2xXr2"
          crossorigin="anonymous"
        />
        <link
          href="https://fonts.googleapis.com/css?family=Source+Code+Pro"
          rel="stylesheet"
        />
        <link
          rel="icon"
          type="image/png"
          href="https://cdn0.iconfinder.com/data/icons/small-n-flat/24/678068-terminal-512.png"
        />
        <style>
          * {
            font-family: 'Source Code Pro', monospace;
            font-size: 1rem !important;
          }
          body {
            background-color: #212529;
          }
          pre {
            color: #cccccc;
          }
          b {
            color: #01b468;
          }
        </style>
      </head>
      <body>
        <table class="table table-dark table-bordered">
          <thead>
            <tr>
              <replace1>
            </tr>
          </thead>
          <tbody>
            <tr>
              <replace2>
            </tr>
          </tbody>
        </table>
      </body>
    </html>
    )";
  boost::replace_all(h, "<replace1>", thead);
  boost::replace_all(h, "<replace2>", tbody);
  cout << h << std::flush;
}

int main(int argc, char *argv[])
{

  try
  {
    boost::asio::io_context io_context;
    session_manager cntl(io_context);
    cntl.parse_query();
    prt_html(cntl.sessions);
    cntl.start();
    io_context.run();
  }
  catch (std::exception &e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
