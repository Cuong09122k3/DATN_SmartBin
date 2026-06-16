import os
import config

# ===============================================================
# CAU HINH LIEN KET TU FILE CONFIG.PY
# ===============================================================
TFLITE_MODEL_PATH = config.TFLITE_MODEL_PATH
HEADER_FILE_PATH = config.HEADER_FILE_PATH
DATASET_PATH = config.DATASET_TRAIN_DIR

# ===============================================================
# HAM HO TRO (HELPER)
# ===============================================================
def get_class_names(dataset_path):
    if os.path.exists(dataset_path):
        names = sorted([d for d in os.listdir(dataset_path) if os.path.isdir(os.path.join(dataset_path, d))])
        if names:
            return names
    return config.CLASS_NAMES

def write_header_file(header_path, tflite_data, class_names):
    os.makedirs(os.path.dirname(header_path), exist_ok=True)
    
    with open(header_path, 'w', encoding='utf-8') as f:
        f.write("// ===============================================================\n")
        f.write("// FILE NAY DUOC SINH TU DONG. KHONG NEN SUA TAY!\n")
        f.write("// ===============================================================\n\n")
        
        f.write("#ifndef MODEL_DATA_H\n")
        f.write("#define MODEL_DATA_H\n\n")

        f.write(f"const int MODEL_NUM_CLASSES = {len(class_names)};\n")
        f.write("const char* const MODEL_CLASS_NAMES[] = { ")
        f.write(", ".join([f'"{name}"' for name in class_names]))
        f.write(" };\n\n")

        arr_len = len(tflite_data)
        f.write(f"const unsigned int g_model_len = {arr_len};\n")
        f.write("const unsigned char g_model[] __attribute__((aligned(8))) = {\n")
        
        for i, byte in enumerate(tflite_data):
            f.write(f"0x{byte:02x}, ")
            if (i + 1) % 12 == 0:
                f.write("\n")
                
        f.write("\n};\n\n")
        f.write("#endif // MODEL_DATA_H\n")

# ===============================================================
# HAM CHINH (MAIN LOGIC)
# ===============================================================
def main():
    if not os.path.exists(TFLITE_MODEL_PATH):
        print(f"LOI: Khong tim thay {TFLITE_MODEL_PATH}.")
        return

    print(f"Dang chuyen doi {TFLITE_MODEL_PATH} sang dang mang C (C-Array)...")

    with open(TFLITE_MODEL_PATH, 'rb') as f:
        tflite_data = f.read()

    class_names = get_class_names(DATASET_PATH)
    if class_names:
        print(f"Tu dong quet ten lop: {class_names}")

    write_header_file(HEADER_FILE_PATH, tflite_data, class_names)
    print(f"Thanh cong! Da cap nhat file Header tai: {HEADER_FILE_PATH}")

if __name__ == "__main__":
    main()
