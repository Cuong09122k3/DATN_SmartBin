import os

# Xac dinh thu muc chua script hien tai (scripts/)
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# ===============================================================
# CAU HINH CHUNG CHO TOAN BO QUY TRINH TINYML
# ===============================================================

# 1. Kich thuoc anh va Batch Size
IMG_SIZE = (128, 128)
BATCH_SIZE = 32

# 2. Duong dan du lieu (Train/Test)
DATASET_TRAIN_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "../dataset/train"))
DATASET_TEST_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, "../dataset/test"))

# 3. Duong dan Model va File ket qua
KERAS_MODEL_PATH = os.path.join(SCRIPT_DIR, "model.keras")
TFLITE_MODEL_PATH = os.path.join(SCRIPT_DIR, "model_quantized.tflite")
EVAL_DIR = os.path.join(SCRIPT_DIR, "evaluation")
HISTORY_PATH = os.path.join(SCRIPT_DIR, "training_history.json")

# 4. Cac tham so huan luyen (Su dung trong train.py)
EPOCHS = 40
FINE_TUNE_EPOCHS = 40
LEARNING_RATE = 0.001
FINE_TUNE_LR = 0.00001
ALPHA = 0.5           # He so mo rong MobileNetV2 (0.5 giup tiet kiem vung nho cho ESP32-S3)
DENSE_UNITS = 32      # So luong neuron lop Dense trung gian (giup giam overfitting va RAM)
SEED = 42             # Hat giong ngau nhien co dinh de dam bao tinh tai lap (Reproducibility)

# 5. Cau hinh bo sung cho export va visualization
HEADER_FILE_PATH = os.path.abspath(os.path.join(SCRIPT_DIR, "../main/model_data.h"))

# Tu dong quet ten lop tu thu muc dataset de thich ung khi ban thay doi ten hoac so luong lop
def get_dataset_classes(dataset_path):
    if os.path.exists(dataset_path):
        classes = sorted([d for d in os.listdir(dataset_path) if os.path.isdir(os.path.join(dataset_path, d))])
        if classes:
            return classes
    return ["Background", "Battery_S1", "Normal_S2"]

CLASS_NAMES = get_dataset_classes(DATASET_TRAIN_DIR)
SAMPLE_CLASS = CLASS_NAMES[-1] if CLASS_NAMES else "Normal_S2"
