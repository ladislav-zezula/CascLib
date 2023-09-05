"""

    We don't ever want to remove any file name from the list, like the community listfile does
    Hence, we will aleays merge an existing listfile with the community one

"""

#!/usr/bin/env python3

import os, re, requests, shutil, sys, zipfile

URL_LISTFILE_CSV = "https://github.com/wowdev/wow-listfile/releases/latest/download/community-listfile-withcapitals.csv"
URL_LISTFILE_TXT = "https://github.com/wowdev/wow-listfile/raw/master/listfile.txt"


def count_letters(single_line):
    uppercase_letters = 0
    lowercase_letters = 0
    for single_char in single_line:
        if 'A' <= single_char and single_char <= 'Z':
            uppercase_letters += 1
        if 'a' <= single_char and single_char <= 'z':
            lowercase_letters += 1
    return uppercase_letters, lowercase_letters


# Convert to upper case and slashes
def normalize_line(single_line):

    norm_line = ""

    # Convert to uppercase and slashes
    for line_token in re.split(r"(/|\\)", single_line):
        if line_token == "\\":
            line_token = "/"
        norm_line = norm_line + line_token.upper()

    return norm_line


def get_more_valuable_line(single_line1, single_line2):

    # Count uppercase chars in both lines
    uppercase_letters1, lowercase_letters1 = count_letters(single_line1)
    uppercase_letters2, lowercase_letters2 = count_letters(single_line2)

    # A "more valuable string" is the one with lowercase and uppercase
    if uppercase_letters1 and lowercase_letters1:
        return single_line1
    if uppercase_letters2 and lowercase_letters2:
        return single_line2

    # If one string is pure uppercase and the other is pure lowercase, take the lowercase one
    if lowercase_letters1 and uppercase_letters1 == 0 and lowercase_letters2 == 0:
        return single_line1
    if lowercase_letters2 and uppercase_letters2 == 0 and lowercase_letters1 == 0:
        return single_line2

    # Doesn't matter
    return single_line2


def add_lines_to_map(lines_map, lines):

    # For each line, add it to map
    for single_line in lines:

        # Get the normalized line. This will serve as key to map
        norm_line = normalize_line(single_line)
        the_better_line = single_line

        # Is the line already there?
        if norm_line in lines_map:
            the_better_line = get_more_valuable_line(lines_map[norm_line], single_line)
        lines_map[norm_line] = the_better_line

    # Return the updated map
    return lines_map

def load_file_to_memory(file_name):
    try:
        # Is it a remote file?
        print("[*] Loading: " + file_name)
        if file_name.startswith("https://"):
            response = requests.get(file_name, timeout=10000)
            if response.status_code != 200:
                print("[x] Download error (%u)" % response.status_code)
                return None
            file_data = response.content.decode("utf-8")
        else:
            with open(file_name, "rt") as f:
                file_data = f.read()

        # Check for valid content
        if file_data is None:
            print("[x] Nothing loaded from " + file_name)
            return None
        if len(file_data) < 65535:
            print("[x] Data length is %u, aborting" % len(file_data))
            return None
        return file_data

    except Exception as e:
        pass
    return None



def download_and_merge(remote_file, local_file, use_csv_sort):

    # Construct the name of the local file
    local_temp_file = local_file + ".temp"
    local_file_data = None
    listfile_map = {}
    response = None

    # Load the remote file
    remote_file_data = load_file_to_memory(remote_file)
    if not remote_file_data:
        return False

    # Load the local file
    local_file_data = load_file_to_memory(local_file)
    if not local_file_data:
        return False

    # Convert both to text
    print("[*] Splitting remote listfile ...")
    remote_file_lines = remote_file_data.splitlines()

    print("[*] Splitting local listfile ...")
    local_file_lines = local_file_data.splitlines()

    print("[*] Merging lines from the remote listfile ...")
    listfile_map = add_lines_to_map(listfile_map, remote_file_lines)

    print("[*] Merging lines from the local listfile ...")
    listfile_map = add_lines_to_map(listfile_map, local_file_lines)

    # Sort the map
    print("[*] Sorting the lines ...")
    if use_csv_sort:
        listfile_map = sorted(listfile_map.items(), key = lambda item: int(item[1].split(";")[0]))
    else:
        listfile_map = sorted(listfile_map.items(), key = lambda item: item[1].lower())

    # Write the final list
    try:
        print("[*] Backuping the listfile ...")
        shutil.copy2(local_file, local_file + ".bak")

        print("[*] Writing the final list ...")
        with open(local_file, "wt") as f:
            for key_value, listfile_line in listfile_map:
                f.write(listfile_line + "\n")

        if os.path.isdir("C:\\Tools64"):
            shutil.copy2(local_file, os.path.join("C:\\Tools64", local_file))

    except Exception as e:
        print("[x] Failed to write the listfile ...")
        return False
    return True

def pack_files_to_zip(zip_file_name, file1, file2):

    # Create a ZIP archive
    try:
        print("[*] Adding listfiles to ZIP ...")
        with zipfile.ZipFile("listfile-archive.zip", "w", compression = zipfile.ZIP_DEFLATED, compresslevel = 8) as zip:
            zip.write(file1)
            zip.write(file2)
        return True
    except Exception as e:
        print("[x] Failed to store listfiles to ZIP")
        pass
    return False


def main(argc, argv):
    global URL_LISTFILE_CSV
    global URL_LISTFILE_TXT

    # Download and merge the CSV+TXT listfiles
    if not download_and_merge(URL_LISTFILE_CSV, "listfile.csv", True):
        return
    if not download_and_merge(URL_LISTFILE_TXT, "listfile.txt", False):
        return

    # Pack the files into ZIP to overcome GitHub's 100 MB limit
    if not pack_files_to_zip("listfile-archive.zip", "listfile.csv", "listfile.txt"):
        return
    print("[*] Complete\n")

if __name__ == "__main__" :
    main(len(sys.argv), sys.argv)
