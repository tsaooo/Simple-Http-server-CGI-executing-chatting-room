#include <stdlib.h>
#include <fstream>
#include <string>
#include <array>
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

void escape(string &s){
    boost::replace_all(s, "\'", "\\\'");
    boost::replace_all(s, "&", "&amp;");
    boost::replace_all(s, ">", "&gt;");
    boost::replace_all(s, "<", "&lt;");
    boost::replace_all(s, "\n", "&NewLine;");
}
class npserver_session
    : public std::enable_shared_from_this<npserver_session>
{
public:
  string host_, port_, id_;
  npserver_session(boost::asio::io_context &ioc,
          string &host, string &port, string &doc, string &id, std::shared_ptr<tcp::socket> cli_sock)
      : host_(host), port_(port), id_(id), doc_(doc), cli_sock_(cli_sock),/*/client_(client)*/ socket_(ioc), resolver_(ioc){}

  void start(){
    string path = "./test_case/" + doc_;
    file.open(path);
    if(file.is_open()) std::cerr << "test file open\n";
    else std::cerr << "test file not exist\n";
    auto self = shared_from_this();
    resolver_.async_resolve(tcp::v4(), host_, port_, std::bind(&npserver_session::on_resolve, this, std::placeholders::_1, std::placeholders::_2));
  }

private:
  string response, doc_;
  std::ifstream file;
  std::shared_ptr<tcp::socket> cli_sock_;
  tcp::socket socket_;
  tcp::resolver resolver_;
  enum{ max_length = 1024};
  std::array<char,max_length> data_;


  void to_client_res(string id, string response){
    escape(response);
    //sprintf(str, "<script>document.getElementById('%s').innerHTML += '%s';</script>", id.c_str(), response.c_str());
    string script = "";
    script += "<script>document.getElementById('" + id + "').innerHTML += '" + response + "';</script>";
    auto self(shared_from_this());
    cli_sock_->async_send(boost::asio::buffer(script),
                            [this, self](boost::system::error_code ec, std::size_t length)
                            {
                              if(!ec){
                                //do_write();
                              }
                              else
                              {
                                std::cerr << "to clinet error: " << ec.message() << endl;
                              }
                            });
  }
  void to_client_cmd(string id, string response)
  {
      escape(response);
      //cout << "<script>document.getElementById('" << id << "').innerHTML += '<b>" << response << "</b>';</script>" << std::flush;
      string script = "";
      script += "<script>document.getElementById('" + id + "').innerHTML += '<b>" + response + "</b>';</script>";
      auto self(shared_from_this());
      cli_sock_->async_send(boost::asio::buffer(script),
                              [this, self](boost::system::error_code ec, std::size_t length)
                              {
                                if(!ec);
                                  //do_write();
                                else
                                  std::cerr << "to clinet error: " << ec.message() << endl;
                              });
  }

  void on_resolve(boost::system::error_code ec, tcp::resolver::results_type endpoints)
  {
    if (!ec)
    {
      auto self = shared_from_this();
      boost::asio::async_connect(socket_, endpoints,
                                 [this, self](boost::system::error_code ec, tcp::endpoint endpoint) {
                                   if (!ec)
                                     do_read();
                                   else
                                     std::cerr << ec.message() << endl;

                                 });
    }
  }

  void do_read()
  {
    auto self(shared_from_this());
    //memset(data_, 0, sizeof(data_));
    socket_.async_read_some(boost::asio::buffer(data_, max_length),
                            [this, self](boost::system::error_code ec, std::size_t length) {
                              if (!ec)
                              {
                                string res(data_.begin(), data_.begin()+length);
                                if (res.find("%") != string::npos){
                                  to_client_res(id_, res);
                                  do_write();
                                }
                                else{
                                  to_client_res(id_, res);
                                  do_read();
                                }
                              }
                              else 
                                std::cerr << "on read error: " << ec.message() << endl;
                            });
  }

  void do_write()
  {
    string cmd;
    if(getline(file, cmd))
    {
      cmd += "\n";
      to_client_cmd(id_, cmd);
      auto self(shared_from_this());
      socket_.async_send(boost::asio::buffer(cmd.c_str(), cmd.size()),
                              [this, self](boost::system::error_code ec, std::size_t /*length*/) {
                                if (!ec){
                                  do_read();
                                }
                                else{
                                  std::cerr << "on write error: " << ec.message() << endl;
                                }
                              });
    }
  }
};

class http_session
  : public std::enable_shared_from_this<http_session>
{
public:
  http_session(std::shared_ptr<tcp::socket> socket, boost::asio::io_context &ioc) 
  : socket_(std::move(socket)), ioc(ioc){}

  void start()
  {
    do_read();
  }

private:
  std::shared_ptr<tcp::socket> socket_;
  boost::asio::io_context &ioc;
  enum { max_length = 1024 };
  char data_[max_length];
  std::vector<std::shared_ptr<npserver_session>> sessions;
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
    std::size_t p;
    string line = data.substr(0, (p = data.find("\r\n")));
    data.erase(0, p+2);
    boost::split(headers, line, boost::is_any_of(" "), boost::token_compress_on);

    auto it = headers.begin();
    d.REQUEST_METHOD = *it;
    it++;
    std::size_t q = (*it).find('?');
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
    std::size_t a  = data.find("\r\n");
    d.HTTP_HOST = data.substr(p, a-p);
  }
  void parse_query()
  {
    std::vector<string> params;
    boost::split(params, d.QUERY_STRING, boost::is_any_of("&"), boost::token_compress_on);

    for (auto it = params.begin(); it != params.end(); it += 3)
    {
      if (!boost::ends_with(*it, "="))
      {
        string host = (*it).substr((*it).find('=')+1);
        string port = (*(it + 1)).substr((*(it + 1)).find('=')+1);
        string doc = (*(it + 2)).substr((*(it + 1)).find('=')+1);
        string id = "s" + (*it).substr(1,1);
        std::cerr << host << " " << port << " " << doc << " " << id << endl;

        //auto cli_sock = std::make_shared<tcp::socket>(socket_);
        //auto self(shared_from_this())
        auto s = std::make_shared<npserver_session>(ioc, host, port, doc, id, socket_);
        sessions.push_back(s);
      }
    }
  }

  void console(){
    parse_query();
    string thead, tbody;
    for(const auto& s:sessions){
      char str1[100];
      char str2[100];
      //sprintf(str1, "<th scope=\"col\">%s:%s</th>\n", s->host_.c_str(), s->port_.c_str());
      thead += "<th scope=\"col\">"+ s->host_ + ":" + s->port_ + "</th>\n";
      //sprintf(str2, "<td><pre id=\"%s\" class=\"mb-0\"></pre></td>\n", s->id_.c_str());
      tbody += "<td><pre id=\"" + s->id_ + "\" class=\"mb-0\"></pre></td>\n";
    }
    string h = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";
    string body = R"(
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
    h += body;
    boost::replace_all(h, "<replace1>", thead);
    boost::replace_all(h, "<replace2>", tbody);
    auto self(shared_from_this());
    socket_->async_send(boost::asio::buffer(h),
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (!ec){
            for(auto &s:sessions)
              s->start();
          }
        });
  }

  void panel(){
    int n_server = 5;
    string test_case_menu;
    string host_menu;
    for(int i=1; i<11; i++)
      test_case_menu += "<option value=\"t" + std::to_string(i) + ".txt\">t" + std::to_string(i) + ".txt</option>";
    for (int i = 1; i < 13; i++){
      host_menu += "<option value=\"nplinux" + std::to_string(i) + ".cs.nctu.edu.tw\">nplinux" + std::to_string(i) + "</option>";
    }
    
    string h = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";
    string body = R"(
      <!DOCTYPE html>
      <html lang="en">
        <head>
          <title>NP Project 3 Panel</title>
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
            href="https://cdn4.iconfinder.com/data/icons/iconsimple-setting-time/512/dashboard-512.png"
          />
          <style>
            * {
              font-family: 'Source Code Pro', monospace;
            }
          </style>
        </head>
        <body class="bg-secondary pt-5">
        <form action="console.cgi" method="GET">
          <table class="table mx-auto bg-light" style="width: inherit">
            <thead class="thead-dark">
              <tr>
                <th scope="col">#</th>
                <th scope="col">Host</th>
                <th scope="col">Port</th>
                <th scope="col">Input File</th>
              </tr>
            </thead>
            <tbody>)";
    h += body;
    for (int i = 0; i<n_server; i++){
      string tr = R"(
        <tr>
          <th scope="row" class="align-middle">Session {i+1}</th>
          <td>
            <div class="input-group">
              <select name="h{i}" class="custom-select">
                <option></option>{host_menu}
              </select>
              <div class="input-group-append">
                <span class="input-group-text">.cs.nctu.edu.tw</span>
              </div>
            </div>
          </td>
          <td>
            <input name="p{i}" type="text" class="form-control" size="5" />
          </td>
          <td>
            <select name="f{i}" class="custom-select">
              <option></option>
              {test_case_menu}
            </select>
          </td>
        </tr>)";
      boost::replace_all(tr, "{i+1}", std::to_string(i+1));
      boost::replace_all(tr, "{i}", std::to_string(i));
      boost::replace_all(tr, "{test_case_menu}", test_case_menu);
      boost::replace_all(tr, "{host_menu}", host_menu);
      h += tr;
    }
    string end = R"(
      <tr>
                  <td colspan="3"></td>
                  <td>
                    <button type="submit" class="btn btn-info btn-block">Run</button>
                  </td>
                </tr>
              </tbody>
            </table>
          </form>
        </body>
      </html>)";
    h += end;
    auto self(shared_from_this());
    socket_->async_send(boost::asio::buffer(h),
        [this, self](boost::system::error_code ec, std::size_t /*length*/)
        {
          if (ec){
            std::cerr << "send panel html error: " << ec.message() << endl;
          }
        });
  }

  void do_read()
  {
    auto self(shared_from_this());
    socket_->async_read_some(boost::asio::buffer(data_, max_length),
        [this, self](boost::system::error_code ec, std::size_t length)
        {
          if (!ec)
          {
            string data = data_;
            parse(data);
            if(d.REQUEST_URI == "panel.cgi"){
              panel();
            }
            else if(d.REQUEST_URI == "console.cgi"){
              console();
            }
            else{
              std::cerr << "client request illegal\n";
            }
          }
        });
  }

};

class server
{
public:
  server(boost::asio::io_context& io_context, short port)
    : ioc(io_context), acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
  {
    do_accept();
  }

private:
  void do_accept()
  {
    auto socket_ = std::make_shared<tcp::socket>(ioc);
    acceptor_.async_accept(*socket_,
        [this, socket_](boost::system::error_code ec)
        {
          if (!ec)
          {
            auto s = std::make_shared<http_session>(std::move(socket_), ioc);
            http_sess.push_back(s);
            s->start();
          }
          do_accept();
        });
  }
  boost::asio::io_context &ioc;
  tcp::acceptor acceptor_;
  std::vector<std::shared_ptr<http_session>> http_sess;
};

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 2)
    {
      std::cerr << "Usage: cgi_server <port>\n";
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
