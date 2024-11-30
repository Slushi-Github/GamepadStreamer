import socket
import threading
import tkinter as tk
from PIL import Image, ImageTk
import io

HOST = '0.0.0.0'
PORT = 4242

current_frame = None
status_message = "Waiting for data..."

def start_server():
    global current_frame, status_message

    server_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    server_socket.bind((HOST, PORT))
    print(f"Server running on {HOST}:{PORT}")

    while True:
        try:
            data, addr = server_socket.recvfrom(65536)
            print(f"received {len(data)} bytes from {addr}")
            status_message = f"Streaming from {addr}"

            image_stream = io.BytesIO(data)
            current_frame = Image.open(image_stream)

        except Exception as e:
            print(f"Error: {e}")
            status_message = "Error: " + str(e)

def update_window(canvas, root, img_label):
    global current_frame, status_message

    if current_frame is not None:
        frame = current_frame.copy()
        frame.thumbnail((800, 480))
        tk_image = ImageTk.PhotoImage(frame)
        img_label.config(image=tk_image)
        img_label.image = tk_image
    else:
        canvas.delete("all")
        canvas.create_text(
            400, 240, text=status_message, font=("Arial", 24), fill="white"
        )

    root.after(100, update_window, canvas, root, img_label)

def main():
    root = tk.Tk()
    root.title("GamePad Streamer")
    root.geometry("854x480")
    root.configure(bg="black")
    root.resizable(width=False,height=False)

    canvas = tk.Canvas(root, width=800, height=480, bg="black", highlightthickness=0)
    canvas.pack(fill="both", expand=True)

    img_label = tk.Label(root, bg="black")
    img_label.place(relx=0.5, rely=0.5, anchor="center")

    server_thread = threading.Thread(target=start_server, daemon=True)
    server_thread.start()

    update_window(canvas, root, img_label)

    root.mainloop()

if __name__ == "__main__":
    main()