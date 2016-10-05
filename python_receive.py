import OSC
import threading

receive_address = '127.0.0.1', 9003


s = OSC.ThreadingOSCServer(receive_address)

s.addDefaultHandlers()

def printing_handler(addr, tags, stuff, source):
        print addr, stuff

s.addMsgHandler("/banana", printing_handler)
s.addMsgHandler("/apple/pie", printing_handler)

def main():
    # Start OSCServer
    print "Starting OSCServer"
    st = threading.Thread(target=s.serve_forever)
    st.start()
main()
