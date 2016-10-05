import OSC
import time

# Init OSC
client = OSC.OSCClient()
msg1 = OSC.OSCMessage()
msg1.setAddress("/test")

msg2 = OSC.OSCMessage()
msg2.setAddress("/another/test")
for i in range(100000):
    time.sleep(1)
    msg1.append(i)
    msg2.append(i)
    try:
        client.sendto(msg1, ('127.0.0.1', 9001))
        msg1.clearData()
        client.sendto(msg2, ('127.0.0.1', 9001))
        msg2.clearData()
    except:
        print 'Connection refused'
