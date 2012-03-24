import ctypes
import socket

import bass.pybass as bass

data_buffer = ""

# BASS callback function
def bass_load(handle, buffer, length, user):
	global data_buffer
	#print str(len(data_buffer)) + " " + str(length)
	
	b = ctypes.cast(buffer, ctypes.c_char_p)
	if len(data_buffer) == 0:
		# copy silence
		ctypes.memset(b, 0, length)		
	else:
		# copy data		
		copy_pointer = ctypes.c_char_p(data_buffer)
		copy_length = min(len(data_buffer), length)
		ctypes.memmove(b, copy_pointer, copy_length)
		
		data_buffer = data_buffer[copy_length:]
	
	return length
bass_load_func = bass.STREAMPROC(bass_load)

# Settings
#UDP_IP, UDP_PORT = "0.0.0.0", 4010
UDP_IP, UDP_PORT = "::", 4010

# Create Socket
sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
sock.bind((UDP_IP,UDP_PORT))

# init bass
bass.BASS_Init(-1, 44100, 0, 0, 0)
if not bass.BASS_SetConfig(bass.BASS_CONFIG_UPDATEPERIOD, 10):
	print bass.get_error_description(bass.BASS_ErrorGetCode())

# create stream
handle = bass.BASS_StreamCreate(44100, 2, 0, bass_load_func, 0)
if handle == 0:
	print bass.get_error_description(bass.BASS_ErrorGetCode())
	
# play it
if not bass.BASS_ChannelPlay(handle, False):
	print bass.get_error_description(bass.BASS_ErrorGetCode())

try:
	last_addr = None
	while True:
		data, addr = sock.recvfrom(2048)
		if last_addr != addr:
			print "New Connection"
			last_addr = addr
		data_buffer += data
except:
	import traceback
	traceback.print_exc()
	
# BASS cleanup
bass.BASS_ChannelStop(handle)
bass.BASS_StreamFree(handle)
bass.BASS_Free()