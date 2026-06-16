import os
import tensorflow as tf
import tf_keras as keras
import numpy as np

# Xac dinh thu muc chua script hien tai
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

import config

# Cau hinh duong dan tu config.py
KERAS_MODEL_PATH = config.KERAS_MODEL_PATH
TFLITE_MODEL_PATH = config.TFLITE_MODEL_PATH
DATASET_DIR = config.DATASET_TRAIN_DIR
IMG_SIZE = config.IMG_SIZE
BATCH_SIZE = config.BATCH_SIZE

def load_representative_dataset(dataset_dir, img_size): # tải tập dữ liệu
    print(f">>> Dang tai tap du lieu dai dien tu: {dataset_dir}")
    if not os.path.exists(dataset_dir):
        raise FileNotFoundError(f"Loi: Khong tim thay thu muc dataset dai dien tai {dataset_dir}")

    train_ds = keras.utils.image_dataset_from_directory(
        dataset_dir,
        image_size=img_size,
        batch_size=BATCH_SIZE,
        shuffle=True,
        seed=config.SEED
    )
    return train_ds

def convert_to_tflite_int8(keras_model_path, tflite_path, train_ds): # Tải mô hình Keras và bắt đầu quá trình chuyển đổi.
    print(f"\n>>> Dang nap mo hinh Keras tu: {keras_model_path}...")
    if not os.path.exists(keras_model_path):
        raise FileNotFoundError(f"Loi: Khong tim thay mo hinh Keras tai {keras_model_path}")
        
    model = keras.models.load_model(keras_model_path)
    
    print("\n>>> Bat dau chuyen doi sang TFLite INT8 (Su dung PTQ)...")
    
    # Tạo dữ liệu đại diện để tính scale/zero-point
    def representative_data_gen():
        count = 0
        for img, _ in train_ds.take(10):  # Lay toi da 10 batches
            for i in range(img.shape[0]):
                if count >= 100:
                    return
                yield [tf.expand_dims(img[i], axis=0)] # thêm batch dimension
                count += 1

    converter = tf.lite.TFLiteConverter.from_keras_model(model) # khởi tạo converter từ mô hình Keras đã tải
    converter.optimizations = [tf.lite.Optimize.DEFAULT] # bật chế độ tối ưu hóa
    converter.representative_dataset = representative_data_gen # gán dữ liệu đại diện
    
    # Thiet lap ep kieu toan phan sang so nguyen 8-bit (INT8)
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8] # chỉ hỗ trợ các phép toán TFLite INT8
    converter.inference_input_type = tf.int8 # ép kiểu đầu vào thành INT8
    converter.inference_output_type = tf.int8 # ép kiểu đầu ra thành INT8
    
    print(">>> Dang chay cong cu chuyen doi TensorFlow Lite...")
    tflite_model = converter.convert()
    
    with open(tflite_path, "wb") as f:
        f.write(tflite_model)
        
    print(f"\n>>> THANH CONG! Da chuyen doi va luu mo hinh TFLite INT8 tai: {tflite_path}")
    print(f"Kich thuoc tep TFLite: {os.path.getsize(tflite_path) / 1024:.2f} KB")

def main():
    try:
        train_ds = load_representative_dataset(DATASET_DIR, IMG_SIZE)
        convert_to_tflite_int8(KERAS_MODEL_PATH, TFLITE_MODEL_PATH, train_ds)
    except Exception as e:
        print(f"\n[LOI CHUYEN DOI]: {e}")
        import sys
        sys.exit(1)

if __name__ == "__main__":
    main()
