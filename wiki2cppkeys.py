#################################################################################
#                                                                               #
#  HOW TO USE THIS SCRIPT                                                       #
#                                                                               #
#  1) Go to https://wowdev.wiki/CASC                                            #
#  2) Copy the entire table of encryption keys into a text file.                #
#     Skip the headings ("key_name    key    type ...")                         #
#  3) Run wiki2cppkeys.py YourTextFileName                                      #
#  4) Diff the result CPP file with the table of keys in CascDecrypt.cpp        #
#  5) Add new keys as necessary                                                 #
#                                                                               #
#################################################################################

import os, sys, string, requests

#def read_wow_keys(url_address):
#
#	# Retrieve the response
#	response = requests.get(url_address)
#	if not response.ok:
#		return ''
#
#	print(response.content)
#	print(len(response.content))

def convert_to_c_array(key_data):

	key_data_c = ''
	start_index = 0

	while(start_index < 32):
		key_data_c = key_data_c + '0x' + key_data[start_index:start_index+2]
		if(start_index < 0x1E):
			 key_data_c = key_data_c + ', '
		start_index += 2

	return key_data_c

def wiki_to_cpp(file_name):

	# Process the lines in the detections file
	file_lines = [line.rstrip('\n') for line in open(file_name)]
	start_index = 0;
	line_no = 1;

	# Create the deletion script
	f = open(file_name + '.cpp', 'wt')

	# Parse all lines
	for line in file_lines:

		# Remove leading whitespaces
		comment_string = '  '
		line = line.lstrip(' ')

		# Extract the key name
		space_index = line.index(' ')
		key_name = line[0:space_index]
		line = line[space_index:].lstrip(' ')

		# Check the key name
		if(len(key_name) != 16):
			print 'Unexpected key name length at line %u' % line_no
			return

		# Extract the key data
		space_index = line.index(' ')
		key_data = line[0:space_index]
		line = line[space_index:].lstrip(' ')

		# Check the key data
		if(len(key_data) != 32):
			print 'Unexpected key data length at line %u' % line_no
			return

		# Extract the "salsa20"
		space_index = line.index(' ')
		salsa20_string = line[0:space_index]
		line = line[space_index:].lstrip(' ')

		# Check for "salsa20"
		if(salsa20_string != "salsa20"):
			print 'Missing "salsa20" string at line %u' % line_no
			return

		# Construct the target line
		if(key_data[0] == '?' or key_data[1] == '?'):
			comment_string = '//'
		target_line = '%s  { 0x%sULL, { %s } },   // %s' % (comment_string, key_name, convert_to_c_array(key_data), line)

		# Write the line to the result file
		f.write(target_line + '\n')
		print target_line
		line_no += 1

	f.close();



def main(argc, argv):

	# The first parameter must be a source filelist
	if(argc < 1):
		print "Missing source file name. Usage: wiki2cpp.py SourceFile"
		return

	# Retrieve the list of WoW keys from the WoW wiki
	# wow_keys = read_wow_keys('https://wowdev.wiki/CASC')

	# Dump the files sorted by hashes
	wiki_to_cpp(argv[1])

if __name__ == "__main__" :
	main(len(sys.argv), sys.argv)
