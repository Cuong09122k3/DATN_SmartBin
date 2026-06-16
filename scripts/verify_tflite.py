import tensorflow as tf
import tf_keras as keras
import numpy as np
import os
import matplotlib.pyplot as plt
import seaborn as sns
from sklearn.metrics import confusion_matrix, classification_report, roc_curve, auc
from sklearn.preprocessing import label_binarize

# ===============================================================
# Cau hinh duong dan va thong so tu config.py
# ===============================================================
import config

KERAS_MODEL_PATH = config.KERAS_MODEL_PATH
TFLITE_MODEL_PATH = config.TFLITE_MODEL_PATH
EVAL_DIR = config.EVAL_DIR
IMG_SIZE = config.IMG_SIZE
BATCH_SIZE = config.BATCH_SIZE

# Uu tien lay du lieu tu thu muc test, neu khong co thi lay train
DATASET_DIR = config.DATASET_TEST_DIR
if not os.path.exists(DATASET_DIR):
    DATASET_DIR = config.DATASET_TRAIN_DIR

def plot_tflite_confusion_matrix(labels, preds, class_names, acc):
    cm = confusion_matrix(labels, preds)
    plt.figure(figsize=(10, 8))
    sns.heatmap(cm, annot=True, fmt='d', cmap='Oranges',
                xticklabels=class_names, yticklabels=class_names,
                annot_kws={"size": 12})
    plt.title(f'Ma trận nhầm lẫn (TFLite INT8)\nAccuracy: {acc*100:.2f}%', fontsize=13, fontweight='bold', pad=12)
    plt.xlabel('Dự đoán', fontsize=12, labelpad=10)
    plt.ylabel('Thực tế', fontsize=12, labelpad=10)
    
    out_path = os.path.join(EVAL_DIR, 'evaluation_tflite_confusion_matrix.png')
    plt.savefig(out_path, bbox_inches='tight')
    print(f">>> Da luu bieu do Confusion Matrix (TFLite) tai: {out_path}")
    plt.close()

def plot_tflite_misclassified_images(images, labels, preds, scores, class_names):
    misclassified_indices = np.where(labels != preds)[0]
    n_total = len(misclassified_indices)
    if n_total == 0:
        print(f">>> Khong co anh nao du doan sai de hien thi (TFLite).")
        return

    print(f"\n>>> Dang ve bieu do tong hop anh du doan sai cua TFLite ({n_total} anh)...")
    
    cols = 4
    rows = int(np.ceil(n_total / cols))
    fig_height = max(6, rows * 3.8)
    plt.figure(figsize=(15, fig_height))
    
    for i, idx in enumerate(misclassified_indices):
        plt.subplot(rows, cols, i + 1)
        display_img = images[idx].astype("uint8")
        plt.imshow(display_img)

        true_label = class_names[labels[idx]]
        pred_label = class_names[preds[idx]]
        
        bg_pct = scores[idx][0] * 100
        bt_pct = scores[idx][1] * 100
        nm_pct = scores[idx][2] * 100

        title_text = (
            f"True: {true_label} | Pred: {pred_label}\n"
            f"BG: {bg_pct:.1f}% | BT: {bt_pct:.1f}% | NM: {nm_pct:.1f}%"
        )

        plt.title(title_text, color='red', fontsize=11, pad=8)
        plt.axis('off')
        
    plt.suptitle(f"Các ảnh phân loại sai (TFLite INT8) - Tổng số: {n_total} ảnh\nBG: Background | BT: Battery_S1 | NM: Normal_S2", fontsize=14, fontweight='bold', y=0.98)
    plt.tight_layout(rect=[0, 0, 1, 0.92])
    plt.subplots_adjust(top=0.88, hspace=0.38)
    
    out_path = os.path.join(EVAL_DIR, 'evaluation_tflite_misclassified.png')
    plt.savefig(out_path, dpi=150)
    print(f">>> Da luu bieu do anh sai TFLite tai: {out_path}")
    plt.close()

def plot_tflite_roc_curve(labels, scores, class_names):
    y_bin = label_binarize(labels, classes=[0, 1, 2])
    n_classes = len(class_names)

    plt.figure(figsize=(10, 8))
    for i in range(n_classes):
        fpr, tpr, _ = roc_curve(y_bin[:, i], scores[:, i])
        roc_auc = auc(fpr, tpr)
        plt.plot(fpr, tpr, lw=2, label=f'Lớp {class_names[i]} (AUC = {roc_auc:.2f})')

    plt.plot([0, 1], [0, 1], color='gray', lw=1, linestyle='--')
    plt.xlim([0.0, 1.0])
    plt.ylim([0.0, 1.05])
    plt.xlabel('False Positive Rate (FPR)', fontsize=12, labelpad=10)
    plt.ylabel('True Positive Rate (TPR)', fontsize=12, labelpad=10)
    plt.title(f'ROC Curve và AUC (TFLite INT8)', fontsize=13, fontweight='bold', pad=15)
    plt.legend(loc="lower right", fontsize=11)
    plt.grid(True, alpha=0.3)
    
    out_path = os.path.join(EVAL_DIR, 'evaluation_tflite_roc_curve.png')
    plt.savefig(out_path, bbox_inches='tight')
    print(f">>> Da luu bieu do ROC (TFLite) tai: {out_path}")
    plt.close()

def verify():
    if not os.path.exists(KERAS_MODEL_PATH) or not os.path.exists(TFLITE_MODEL_PATH):
        print(f"LOI: Khong tim thay model de kiem tra!")
        print(f"Duong dan Keras: {KERAS_MODEL_PATH}")
        print(f"Duong dan TFLite: {TFLITE_MODEL_PATH}")
        return

    print("===============================================================")
    print("      BAT DAU KIEM TRA DOI CHIEU MO HINH: KERAS VS TFLITE      ")
    print("===============================================================")
    print(f"Thu muc du lieu su dung: {DATASET_DIR}")
    
    # Tao thu muc danh gia neu chua co
    os.makedirs(EVAL_DIR, exist_ok=True)
    
    # 1. Nap mo hinh Keras goc
    print("\n>>> Dang nap mo hinh Keras...")
    keras_model = keras.models.load_model(KERAS_MODEL_PATH, compile=False, safe_mode=False)
    
    # 2. Thiet lap trinh thong dich TFLite
    print(">>> Dang nap mo hinh TFLite...")
    interpreter = tf.lite.Interpreter(model_path=TFLITE_MODEL_PATH)
    interpreter.allocate_tensors()
    
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    
    # 3. Lay toan bo du lieu tu Dataset
    print(">>> Dang nap tap du lieu kiem thu...")
    dataset = keras.utils.image_dataset_from_directory(
        DATASET_DIR,
        image_size=IMG_SIZE,
        batch_size=BATCH_SIZE,
        shuffle=False
    )
    class_names = dataset.class_names
    print(f"Danh sach nhan lop hoc: {class_names}")

    all_images = []
    all_labels = []
    for images, labels in dataset:
        all_images.append(images.numpy())
        all_labels.append(labels.numpy())
    
    images_np = np.vstack(all_images)
    labels_np = np.concatenate(all_labels)
    total_samples = len(images_np)
    print(f"Tong so mau thu nghiem: {total_samples}")

    # --- A. Du doan bang Keras tren toan bo tap du lieu (Nhanh) ---
    print("\n>>> Keras dang chay du doan tren toan bo tap du lieu...")
    keras_preds = keras_model.predict(images_np, batch_size=BATCH_SIZE, verbose=0)
    keras_class_indices = np.argmax(keras_preds, axis=1)

    # --- B. Du doan bang TFLite tung anh mot ---
    print(">>> TFLite dang chay suy luan tung anh...")
    tflite_preds = []
    for i in range(total_samples):
        test_img = np.expand_dims(images_np[i], axis=0)
        tflite_input = test_img.copy()
        if input_details['dtype'] == np.int8:
            scale, zero_point = input_details['quantization']
            tflite_input = np.array(test_img / scale + zero_point, dtype=np.int8)
            
        interpreter.set_tensor(input_details['index'], tflite_input)
        interpreter.invoke()
        
        tflite_output = interpreter.get_tensor(output_details['index'])[0]
        
        if output_details['dtype'] == np.int8:
            out_scale, out_zero_point = output_details['quantization']
            preds_i = (tflite_output.astype(np.float32) - out_zero_point) * out_scale
        else:
            preds_i = tflite_output
        tflite_preds.append(preds_i)
        
    tflite_preds = np.array(tflite_preds)
    tflite_class_indices = np.argmax(tflite_preds, axis=1)

    # 4. Tinh toan do chinh xac va do tuong quan
    keras_acc = np.mean(keras_class_indices == labels_np)
    tflite_acc = np.mean(tflite_class_indices == labels_np)
    match_rate = np.mean(keras_class_indices == tflite_class_indices)
    
    print("\n===============================================================")
    print("                     TONG KET DOI CHIEU                        ")
    print("===============================================================")
    print(f"Keras Accuracy (Float32) : {keras_acc*100:.2f}%")
    print(f"TFLite Accuracy (INT8)   : {tflite_acc*100:.2f}%")
    print(f"Ti le khop nhan du doan  : {match_rate*100:.2f}%")
    
    # 5. In 5 mau thu tieu bieu ra console de nguoi dung theo doi truc quan
    print("\n--- Vi du 5 mau thu tieu bieu ---")
    sample_indices = np.linspace(0, total_samples-1, 5, dtype=int)
    for idx, s_idx in enumerate(sample_indices):
        true_label_name = class_names[labels_np[s_idx]]
        k_pred_name = class_names[keras_class_indices[s_idx]]
        t_pred_name = class_names[tflite_class_indices[s_idx]]
        k_conf = keras_preds[s_idx][keras_class_indices[s_idx]]
        t_conf = tflite_preds[s_idx][tflite_class_indices[s_idx]]
        status = "TRUNG KHOP" if keras_class_indices[s_idx] == tflite_class_indices[s_idx] else "SAI LECH"
        print(f"Mau {idx+1} [Index {s_idx}] | Nhan goc: {true_label_name}")
        print(f"  * Keras:  {k_pred_name:<12} (Conf = {k_conf*100:.2f}%)")
        print(f"  * TFLite: {t_pred_name:<12} (Conf = {t_conf*100:.2f}%)")
        print(f"  * Trang thai: {status}")
    
    # 6. Ve bieu do so sanh va kiem chung do chinh xac
    print("\n>>> Dang tao va luu bieu do kiem chung...")
    
    # Tinh toan do chinh xac tren tung lop hoc
    class_keras_acc = []
    class_tflite_acc = []
    for c_idx, c_name in enumerate(class_names):
        class_mask = (labels_np == c_idx)
        if np.sum(class_mask) > 0:
            c_k_acc = np.mean(keras_class_indices[class_mask] == c_idx)
            c_t_acc = np.mean(tflite_class_indices[class_mask] == c_idx)
        else:
            c_k_acc = 0.0
            c_t_acc = 0.0
        class_keras_acc.append(c_k_acc * 100)
        class_tflite_acc.append(c_t_acc * 100)
        
    plt.figure(figsize=(8, 10))
    
    # Subplot 1: So sanh do chinh xac tong quan (Overall Accuracy)
    plt.subplot(2, 1, 1)
    models = ['Keras (Float32)', 'TFLite (INT8)']
    values = [keras_acc * 100, tflite_acc * 100]
    colors = ['#1f77b4', '#2ca02c']
    
    bars = plt.bar(models, values, color=colors, width=0.4)
    # Su dung tieng Anh thuan nhat cho nhan truc, nhung tieu de bieu do tieng Viet
    plt.ylabel('Accuracy (%)', fontsize=12)
    plt.title('So sánh độ chính xác tổng thể', fontsize=13, fontweight='bold', pad=15)
    plt.ylim(0, 110)
    plt.grid(axis='y', linestyle='--', alpha=0.5)
    
    # Ghi chu so tren dau bar
    for bar in bars:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2.0, height + 1.5, f'{height:.2f}%', ha='center', va='bottom', fontsize=10)
        
    # Subplot 2: So sanh do chinh xac theo tung lop (Class-by-Class Accuracy)
    plt.subplot(2, 1, 2)
    x = np.arange(len(class_names))
    width = 0.35
    
    bars_keras = plt.bar(x - width/2, class_keras_acc, width, label='Keras (Float32)', color='#1f77b4')
    bars_tflite = plt.bar(x + width/2, class_tflite_acc, width, label='TFLite (INT8)', color='#2ca02c')
    
    # Su dung tieng Anh thuan nhat cho nhan truc, nhung tieu de bieu do tieng Viet
    plt.ylabel('Accuracy (%)', fontsize=12)
    plt.title('So sánh độ chính xác theo từng lớp', fontsize=13, fontweight='bold', pad=15)
    plt.xticks(x, class_names, fontsize=11)
    plt.ylim(0, 110)
    plt.legend(loc='lower right')
    plt.grid(axis='y', linestyle='--', alpha=0.5)
    
    # Ghi chu so tren dau bar cho Keras
    for bar in bars_keras:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2.0, height + 1.5, f'{height:.2f}%', ha='center', va='bottom', fontsize=9)
        
    # Ghi chu so tren dau bar cho TFLite
    for bar in bars_tflite:
        height = bar.get_height()
        plt.text(bar.get_x() + bar.get_width()/2.0, height + 1.5, f'{height:.2f}%', ha='center', va='bottom', fontsize=9)
        
    # Dat tieu de suptitle lon tren cung va can thiet lay-out
    plt.suptitle('So sánh hiệu năng Keras vs TFLite', fontsize=14, fontweight='bold', y=0.98)
    plt.tight_layout(rect=[0, 0, 1, 0.94], pad=3.0)

    out_img_path = os.path.join(EVAL_DIR, 'evaluation_comparison.png')
    plt.savefig(out_img_path, dpi=150)
    plt.close()
    
    print(f"\n>>> Da luu bieu do kiem chung tai: {out_img_path}")
    
    # Goi cac ham ve bieu do danh gia cho TFLite
    print("\n>>> Dang tao va luu cac bieu do kiem chung chi tiet cho TFLite...")
    plot_tflite_confusion_matrix(labels_np, tflite_class_indices, class_names, tflite_acc)
    plot_tflite_misclassified_images(images_np, labels_np, tflite_class_indices, tflite_preds, class_names)
    plot_tflite_roc_curve(labels_np, tflite_preds, class_names)
    
    print("===============================================================")

if __name__ == "__main__":
    verify()
