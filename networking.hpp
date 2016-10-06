#include <mutex>
#include <cstdlib>
#include <string>
#include <boost/asio.hpp>
#include <boost/utility/string_view.hpp>
#include <thread>
#include <unordered_map>
#include <chrono>
#include <iostream>

using boost::asio::ip::udp;

// An Open Sound Control-like message
// See http://opensoundcontrol.org/spec-1_0
struct message
{
    message(boost::string_view a, int32_t v): address{a}, value{v} {}
    message(boost::string_view s)
    {
        // First find address
        auto addr_end = s.find_first_of('\0');
        if(addr_end != std::string::npos)
        {
            address = s.substr(0, addr_end);
        }
        
        // Then parse the value
        auto val_end = s.find_first_of(',');
        if(val_end != std::string::npos && val_end + 8 <= s.size())
        {
            value = ntohl(*reinterpret_cast<const uint32_t*>(s.data() + val_end + 4));
        }
    }
    boost::string_view address;
    int32_t value;
};

// Boo globals... it's only for the sake of the example.
std::unordered_map<std::string, int32_t> input_values;
std::unordered_map<std::string, int32_t> output_values;
std::mutex input_mutex, output_mutex;


// This will receive messages from the python_send.py script
class server
{
public:
  server(boost::asio::io_service& io_service)
    : socket_(io_service, udp::endpoint(udp::v4(), 9001))
  {
    do_receive();
  }

  void do_receive()
  {
    udp::endpoint endpoint;
    socket_.async_receive_from(
        boost::asio::buffer(data_, max_length), endpoint,
        [this](boost::system::error_code ec, std::size_t bytes_recvd)
        {
          if (!ec && bytes_recvd > 0)
          {
            message m(boost::string_view(data_, bytes_recvd));
            std::lock_guard<std::mutex> l(input_mutex);
            input_values[m.address.to_string()] = m.value;
           // std::cerr << m.address.to_string() << " " << m.value << std::endl;
          }
            do_receive();
        });
  }
  
private:
  udp::socket socket_;
  enum { max_length = 1024 };
  char data_[max_length];
};


// This will send messages to the python_receive.py
class sender
{
    public:
    sender():
        s(io_service, udp::endpoint(udp::v4(), 0)),
        endpoint(*udp::resolver{io_service}.resolve({udp::v4(), "127.0.0.1", "9003"}))
    {
    }
    
    void send(message m)
    {
        char request[1024] = {0};
        std::copy(m.address.begin(), m.address.end(), std::begin(request));
        auto n = m.address.size();
        while(n % 4 != 0)
        {
            request[n++] = 0;
        }
        request[n++] = ',';
        request[n++] = 'i';
        request[n++] = 0;
        request[n++] = 0;        
        
        auto v = htonl(m.value);
        auto v_p = reinterpret_cast<const char*>(&v);
        std::copy_n(v_p, 4, request + n);
        
        for(int i = 0; i < n + 8; i ++)
            std::cerr << ( request[i] < 32 ? '$' : request[i] )<< " ";
            std::cerr << std::endl;
                
        s.send_to(boost::asio::buffer(request, n + 8), endpoint);
    }
        
    boost::asio::io_service io_service;
    udp::socket s;
    udp::endpoint endpoint;
};
