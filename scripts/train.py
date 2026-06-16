import os
import json
import random
import numpy as np
import tensorflow as tf
import config

# Co dinh seed tu config.py de dam bao tinh tai lap (Reproducibility)
SEED = config.SEED
random.seed(SEED)
np.random.seed(SEED)
tf.random.set_seed(SEED)

import tf_keras as keras
from tf_keras import layers, models, applications, optimizers, callbacks

# Xac dinh thu muc chua script hien tai
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# ===============================================================
# CAU HINH HUAN LUYEN CO BAN (Gom tu config.py)
# ===============================================================
CONFIG = {
    "dataset_dir": config.DATASET_TRAIN_DIR,
    "img_size": config.IMG_SIZE,
    "batch_size": config.BATCH_SIZE,
    "epochs": config.EPOCHS,
    "fine_tune_epochs": config.FINE_TUNE_EPOCHS,
    "learning_rate": config.LEARNING_RATE,
    "fine_tune_lr": config.FINE_TUNE_LR,
    "alpha": config.ALPHA,
    "dense_units": config.DENSE_UNITS,
    "save_path": config.KERAS_MODEL_PATH
}

def build_basic_model(input_shape, num_classes):
    # 1. Tien xu ly va Tang cuong du lieu (Data Augmentation)
    data_augmentation = keras.Sequential([
        layers.RandomFlip("horizontal"),
        layers.RandomRotation(0.5),
        layers.RandomTranslation(height_factor=0.15, width_factor=0.15),
        layers.RandomZoom(0.2),
        layers.RandomContrast(0.3),
        layers.RandomBrightness(0.15),
        layers.GaussianNoise(0.05),
    ], name="Data_Augmentation")

    # 2. Khoi tao mo hinh nen MobileNetV2
    base_model = applications.MobileNetV2(
        input_shape=input_shape,
        alpha=CONFIG["alpha"],
        include_top=False,
        weights='imagenet'
    )
    base_model.trainable = False

    # 3. Xay dung kien truc chuoi (Pipeline)
    inputs = layers.Input(shape=input_shape)
    x = data_augmentation(inputs)
    x = applications.mobilenet_v2.preprocess_input(x)
    x = base_model(x, training=False)
    
    # 4. Them cac lop phan loai tuy chinh (Classification Head toi gian ket hop GAP + GMP)
    x = layers.GlobalAveragePooling2D()(x)
    x = layers.Dense(CONFIG["dense_units"], activation='relu')(x)
    x = layers.Dropout(0.3)(x) # Them Dropout de chong hoc vet (Overfitting)
    outputs = layers.Dense(num_classes, activation='softmax')(x)
    
    model = models.Model(inputs, outputs)
    return model, base_model

# ===============================================================
# CAC HAM HO TRO (HELPER)
# ===============================================================
def load_datasets(dataset_dir, img_size, batch_size, seed):
    # Tai tap du lieu va phan chia 80% Training - 20% Validation
    train_ds = keras.utils.image_dataset_from_directory(
        dataset_dir, validation_split=0.2, subset="training", seed=seed,
        image_size=img_size, batch_size=batch_size
    )
    val_ds = keras.utils.image_dataset_from_directory(
        dataset_dir, validation_split=0.2, subset="validation", seed=seed,
        image_size=img_size, batch_size=batch_size
    )
    
    # Luu lai class_names cua dataset goc truoc khi ap dung prefetch/cache
    class_names = train_ds.class_names
    
    # Toi uu hoa luong du lieu nap tu o cung vao RAM va GPU/CPU
    AUTOTUNE = tf.data.AUTOTUNE
    train_ds = train_ds.cache().prefetch(buffer_size=AUTOTUNE)
    val_ds = val_ds.cache().prefetch(buffer_size=AUTOTUNE)
    
    return train_ds, val_ds, class_names


def save_training_history(h1, h2, output_path):
    # Chuyen doi cac gia tri numpy sang float Python de JSON serialize duoc
    def convert(obj):
        if isinstance(obj, dict):
            return {k: convert(v) for k, v in obj.items()}
        if isinstance(obj, list):
            return [convert(v) for v in obj]
        if isinstance(obj, (np.floating, np.integer)):
            return float(obj)
        return obj

    history_dict = {
        "phase1": convert(h1.history),
        "phase2": convert(h2.history)
    }
    with open(output_path, "w") as f:
        json.dump(history_dict, f)
    print(f">>> Da luu du lieu lich su huan luyen: {output_path}")

# ===============================================================
# HAM CHINH (MAIN LOGIC)
# ===============================================================
def main():
    # --- KHOI TAO DU LIEU ---
    train_ds, val_ds, class_names = load_datasets(
        CONFIG["dataset_dir"], CONFIG["img_size"], CONFIG["batch_size"], SEED
    )
    num_classes = len(class_names)

    # --- KHOI TAO MO HINH ---
    model, base_model = build_basic_model(CONFIG["img_size"] + (3,), num_classes)
    loss_fn = keras.losses.SparseCategoricalCrossentropy()

    # ---------------------------------------------------------------
    # PHASE 1: Huan luyen lop Dense
    # ---------------------------------------------------------------
    # Chi cap nhat trong so o cac lop Dense tu them, giu nguyen MobileNetV2
    print("\n>>> BAT DAU PHASE 1: Huan luyen lop phan loai...")
    model.compile(optimizer=optimizers.Adam(learning_rate=CONFIG["learning_rate"]), loss=loss_fn, metrics=['accuracy'])
    
    # Su dung EarlyStopping de dung som neu lop phan loai da hoi tu
    early_stop_p1 = callbacks.EarlyStopping(monitor='val_loss', patience=10, restore_best_weights=True) # Dừng huấn luyện sớm nếu validation loss không cải thiện trong 10 epoch
    h1 = model.fit(train_ds, validation_data=val_ds, epochs=CONFIG["epochs"], callbacks=[early_stop_p1]) 

    # ---------------------------------------------------------------
    # PHASE 2: Fine-tuning mot phan mo hinh
    # ---------------------------------------------------------------
    # Mo khoa MobileNetV2 nhung chi fine-tune 30 lop cuoi de chong overfitting
    print("\n>>> BAT DAU PHASE 2: Fine-tuning mot phan mo hinh...")
    base_model.trainable = True
    for layer in base_model.layers[:-30]:
        layer.trainable = False
    model.compile(optimizer=optimizers.Adam(learning_rate=CONFIG["fine_tune_lr"]), loss=loss_fn, metrics=['accuracy']) 
    h2 = model.fit(
        train_ds, validation_data=val_ds, epochs=CONFIG["fine_tune_epochs"],
        callbacks=[
            callbacks.EarlyStopping(monitor='val_loss', patience=10, restore_best_weights=True), # Dừng huấn luyện sớm nếu validation loss không cải thiện trong 10 epoch
            callbacks.ReduceLROnPlateau(monitor='val_loss', factor=0.5, patience=5, min_lr=1e-7), # Giảm learning rate khi validation loss không cải thiện trong 5 epoch
            callbacks.ModelCheckpoint(CONFIG["save_path"], monitor='val_loss', save_best_only=True, mode='min') # Lưu mô hình tốt nhất
        ]
    )

    # ---------------------------------------------------------------
    # LUU MO HINH
    # ---------------------------------------------------------------
    model.save(CONFIG["save_path"])
    
    # Luon luu lich su huan luyen xong la hoan thanh
    history_json_path = config.HISTORY_PATH
    save_training_history(h1, h2, history_json_path)

if __name__ == "__main__":
    main()
