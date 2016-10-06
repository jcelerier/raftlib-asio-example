#include "networking.hpp"
#include <DSPatch.h>

class message_input : public DspComponent
{
public:
    message_input(std::string a) :
     DspComponent(),
     address{a}
    {
        AddOutput_("0");
    }

    void Process_(DspSignalBus& inputs, DspSignalBus& outputs) override
    {
        std::lock_guard<std::mutex> l(input_mutex);
        auto it = input_values.find(address);
            
        if(it != input_values.end())
        {
            outputs.SetValue(0, it->second);
        }
    }
    
    std::string address;
    std::atomic_bool go{false};
};


class message_output : public DspComponent
{
public:
    message_output(std::string a) :
     DspComponent(),
     address{a}
    {
        AddInput_("0");
    }

    void Process_(DspSignalBus& inputs, DspSignalBus& outputs) override
    {
        int32_t v;
        if (inputs.GetValue(0, v))
        {
            std::lock_guard<std::mutex> l(output_mutex);
        
            // Here two things are possible : write to output_values and then send 
            // the content of output_values at each tick from another place        
            output_values[address] = v;
        
            // Or send the message when a value was processed.
            s.send(message{address, v});
        }
    }
    
    std::string address;
    sender s;
};

class multiply_by_ten : public DspComponent
{
public:
    multiply_by_ten() :
     DspComponent()
    {
        AddInput_("0");
        AddOutput_("0");
    }

    void Process_(DspSignalBus& inputs, DspSignalBus& outputs) override
    {
        int32_t v;
        if (inputs.GetValue(0, v))
        {
            outputs.SetValue(0, 10 * v);
        }
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
    
    DspCircuit circuit;
    circuit.SetThreadCount(1);
    circuit.AddComponent(in_1);
    circuit.AddComponent(in_2);
    circuit.AddComponent(out_1);
    circuit.AddComponent(out_2);
    for(auto& c : mult) { circuit.AddComponent(c); }
    
    circuit.ConnectOutToIn(in_1, 0, mult[0], 0);
    circuit.ConnectOutToIn(mult[0], 0, out_1, 0);
    
    circuit.ConnectOutToIn(in_2, 0, mult[1], 0);
    circuit.ConnectOutToIn(mult[1], 0, mult[2], 0);
    circuit.ConnectOutToIn(mult[2], 0, mult[3], 0);
    circuit.ConnectOutToIn(mult[3], 0, out_2, 0);
    
    while(true)
    {
        // Generally it's more precise to do a busy wait if we're under a few milliseconds
        // but let's just assume that this works
        std::this_thread::sleep_for(100ms); 
        
        circuit.Tick();
        circuit.Reset();
    }
        
    DSPatch::Finalize();
    return( EXIT_SUCCESS );
}
