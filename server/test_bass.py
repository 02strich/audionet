import ctypes
import time
import bass.pybass as bass

# load data from file
data = ""
data_pos = 0
with open('test.data', 'rb') as f:
	while True:
		chunk = f.read(4096)
		if chunk:
			data += chunk
		else:
			break
datac = ctypes.create_string_buffer(data)

print len(datac)

def user_stream_callback_function(handle, buffer, length, user):
	global data_pos
	print 'fun'
	print length
	
	# check whether we have enough data left
	if data_pos + length > len(data):
		length |= BASS_STREAMPROC_END
		return length
	
	# copy data
	b = ctypes.cast(buffer, ctypes.c_char_p)
	ctypes.memset(b, 0, length)
	data_pointer = ctypes.cast(ctypes.byref(datac, data_pos), ctypes.POINTER(ctypes.c_char))
	ctypes.memmove(b, data_pointer, length)
	data_pos += length
	
	return length
user_func = bass.STREAMPROC(user_stream_callback_function)


# init bass
bass.BASS_Init(-1, 44100, 0, 0, 0)

# create stream
handle = bass.BASS_StreamCreate(44100, 2, 0, user_func, 0)
if handle == 0:
	print bass.get_error_description(bass.BASS_ErrorGetCode())

if not bass.BASS_ChannelSetAttribute(handle, bass.BASS_ATTRIB_NOBUFFER, 1):
	print bass.get_error_description(bass.BASS_ErrorGetCode())
	
# play it
if not bass.BASS_ChannelPlay(handle, False):
	print bass.get_error_description(bass.BASS_ErrorGetCode())

time.sleep(10)

bass.BASS_ChannelStop(handle)
bass.BASS_StreamFree(handle)
bass.BASS_Free()