#include <raft>
#include <raftio>
#include <cstdlib>
#include <string>
#include <boost/asio.hpp>
#include <boost/utility/string_view.hpp>
#include <thread>
#include <unordered_map>
#include <chrono>

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
        
        union{
            int32_t i;
            char c[4];
        } u;
        u.i = m.value;

        request[n + 3] = u.c[0];
        request[n + 2] = u.c[1];
        request[n + 1] = u.c[2];
        request[n + 0] = u.c[3];
        
        for(int i = 0; i < n + 8; i ++)
            std::cerr << ( request[i] < 32 ? '$' : request[i] )<< " ";
            std::cerr << std::endl;
                
        s.send_to(boost::asio::buffer(request, n + 8), endpoint);
    }
        
    boost::asio::io_service io_service;
    udp::socket s;
    udp::endpoint endpoint;
};


// On each "tick", take the current input of the input_values map.
class message_input : public raft::kernel
{
public:
    message_input(std::string a) :
     raft::kernel(),
     address{a}
    {
       output.addPort< int32_t >("0");
    }

    raft::kstatus run() override
    {
        // Artificial synchronization
        if(go)
        {
            std::lock_guard<std::mutex> l(input_mutex);
            auto it = input_values.find(address);
            
            if(it != input_values.end())
            {
                output["0"].push(it->second);
            }
            go = false;
        }
        
        return raft::proceed; 
    }
    
    std::string address;
    std::atomic_bool go{false};
};


class message_output : public raft::kernel
{
public:
    message_output(std::string a) :
     raft::kernel(),
     address{a}
    {
       input.addPort< int32_t >("0");
    }

    raft::kstatus run() override
    {
        int32_t v;
        input["0"].pop(v);
            
        std::lock_guard<std::mutex> l(output_mutex);
        
        // Here two things are possible : write to output_values and then send 
        // the content of output_values at each tick from another place        
        output_values[address] = v;
        
        // Or send the message when a value was processed.
        s.send(message{address, v});
        
        return raft::proceed; 
    }
    
    std::string address;
    sender s;
};



class multiply_by_ten : public raft::kernel
{
public:
    multiply_by_ten() : raft::kernel()
    {
       input.addPort< int32_t >( "0" ); 
       output.addPort< int32_t >( "0" ); 
    }

    raft::kstatus run() override
    {
        int32_t v;
        input["0"].pop(v);
        output["0"].push(10 * v);
        return raft::proceed; 
    }
};

int main( int argc, char **argv )
{
    using namespace std::literals;
    
    // Create our small server that will receive data from the python
    // script
    boost::asio::io_service io_service;
    server s(io_service);

    std::thread server_thread([&] { io_service.run(); });

    // Some processing 
    message_input in_1{"/test"}, 
                  in_2{"/another/test"};
    message_output out_1{"/banana"}, 
                  out_2{"/apple/pie"};
                  
    std::array<multiply_by_ten, 5> mult;
    raft::print< int32_t, '\n' > p1, p2;
    raft::map m;
    
    m += in_1 >> mult[0] >> out_1;
    m += in_2 >> mult[1] >> mult[2] >> mult[3] >> out_2;
    
    // Thread that simulates a synchronized behaviour
    std::thread t([&] {
        while(true)
        {
            // Generally it's more precise to do a busy wait if we're under a few milliseconds
            // but let's just assume that this works
            std::this_thread::sleep_for(100ms); 
            
            // Instead of doing this, I'd like to run "one" turn of 
            // everything in the raft::map, e.g. "take input, process, write output" 
            // explicitely at this point
            in_1.go = true;
            in_2.go = true;
        }
    });
    
    m.exe();
    return( EXIT_SUCCESS );
}
