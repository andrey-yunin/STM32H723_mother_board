
import os

file_path = '/home/andrey/STM32CubeIDE/workspace_1.19.0/STM32H723_mother_board/App_user/test_combined_commands.py'

with open(file_path, 'r') as f:
    content = f.read()

# Modify wait_for_data_and_done to print raw DONE packet
old_done_print = '                            print(f"Получен DONE для 0x{command_code:04x} со статусом 0x{done_status:04x}.")'
new_done_print = '                            print(f"Получен DONE для 0x{command_code:04x} со статусом 0x{done_status:04x}. Raw: {msg["content"]["raw_packet"].hex(" ")}")'
content = content.replace(old_done_print, new_done_print, 1)

# Modify listen_serial_port to print all raw binary packets put into queue
old_binary_put = '                            received_messages_queue.put({"type": "binary", "content": response_info})'
new_binary_put = old_binary_put + '''
                            print(f"DEBUG_RAW_BIN_PUT: {response_info['raw_packet'].hex(' ')}")'''
content = content.replace(old_binary_put, new_binary_put, 1)

with open(file_path, 'w') as f:
    f.write(content)

print("Modification script finished.")
