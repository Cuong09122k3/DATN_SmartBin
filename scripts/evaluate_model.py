import os
import json
import numpy as np
import tensorflow as tf
import tf_keras as keras
import matplotlib.pyplot as plt
import seaborn as sns
from sklearn.metrics import confusion_matrix, classification_report, roc_curve, auc
from sklearn.preprocessing import label_binarize

# Xac dinh thu muc chua script hien tai
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

import config

# Cau hinh duong dan tu config.py
EVAL_DIR = config.EVAL_DIR
DATASET_DIR = config.DATASET_TEST_DIR
IMG_SIZE = config.IMG_SIZE
BATCH_SIZE = config.BATCH_SIZE
KERAS_MODEL_PATH = config.KERAS_MODEL_PATH
HISTORY_PATH = config.HISTORY_PATH

def load_data():
    print(f">>> Dang tai du lieu tu: {DATASET_DIR}")
    if not os.path.exists(DATASET_DIR):
        print(f"Loi: Thu muc dataset {DATASET_DIR} khong ton tai!")
        return None, None, None

    # Load toan bo du lieu trong thu muc test (khong chia split)
    val_ds = keras.utils.image_dataset_from_directory(
        DATASET_DIR, 
        label_mode='int',
        image_size=IMG_SIZE, 
        batch_size=BATCH_SIZE,
        shuffle=False
    )
    class_names = val_ds.class_names

    all_images = []
    all_labels = []
    for images, labels in val_ds:
        all_images.append(images.numpy())
        all_labels.append(labels.numpy())

    if not all_images:
        return None, None, None

    return np.vstack(all_images), np.concatenate(all_labels), class_names

def evaluate_keras(model_path, images, labels):
    print(f"\n>>> Dang danh gia mo hinh Keras: {model_path}...")
    model = keras.models.load_model(model_path)

    preds = model.predict(images)
    pred_labels = np.argmax(preds, axis=1)

    acc = np.mean(pred_labels == labels)
    print(f"Keras Accuracy: {acc*100:.2f}%")
    return pred_labels, preds, acc



def plot_confusion_matrix(labels, preds, class_names, acc, model_name="Keras (Float32)"):
    cm = confusion_matrix(labels, preds)
    plt.figure(figsize=(10, 8))
    sns.heatmap(cm, annot=True, fmt='d', cmap='Blues',
                xticklabels=class_names, yticklabels=class_names,
                annot_kws={"size": 12})
    # Thong nhat kieu tieu de va nhan truc ngon ngu thuan nhat
    plt.title(f'Ma trận nhầm lẫn\nAccuracy: {acc*100:.2f}%', fontsize=13, fontweight='bold', pad=12)
    plt.xlabel('Dự đoán', fontsize=12, labelpad=10)
    plt.ylabel('Thực tế', fontsize=12, labelpad=10)
    
    out_path = os.path.join(EVAL_DIR, 'evaluation_confusion_matrix.png')
    plt.savefig(out_path, bbox_inches='tight')
    print(f">>> Da luu bieu do Confusion Matrix tai: {out_path}")
    plt.close()



def plot_misclassified_images(images, labels, preds, scores, class_names, model_name="Keras (Float32)"):
    misclassified_indices = np.where(labels != preds)[0]
    n_total = len(misclassified_indices)
    if n_total == 0:
        print(f">>> Khong co anh nao du doan sai de hien thi ({model_name}).")
        return

    print(f"\n>>> Dang ve bieu do tong hop tat ca anh du doan sai ({n_total} anh)...")
    
    cols = 4
    rows = int(np.ceil(n_total / cols))
    
    # Kich thuoc chieu cao vua phai giup bieu do gon gang, cac hang khong bi qua xa
    fig_height = max(6, rows * 3.8)
    plt.figure(figsize=(15, fig_height))
    
    for i, idx in enumerate(misclassified_indices):
        plt.subplot(rows, cols, i + 1)
        display_img = images[idx].astype("uint8")
        plt.imshow(display_img)

        true_label = class_names[labels[idx]]
        pred_label = class_names[preds[idx]]
        
        # Trich xuat score cua ca 3 lop
        bg_pct = scores[idx][0] * 100
        bt_pct = scores[idx][1] * 100
        nm_pct = scores[idx][2] * 100

        # Tieu de ro net va can doi
        title_text = (
            f"True: {true_label} | Pred: {pred_label}\n"
            f"BG: {bg_pct:.1f}% | BT: {bt_pct:.1f}% | NM: {nm_pct:.1f}%"
        )

        plt.title(title_text, color='red', fontsize=11, pad=8)
        plt.axis('off')
        
    # Dat tieu de suptitle o vi tri hop ly cung chu giai nhan dung ten nhan dac trung
    plt.suptitle(f"Các ảnh phân loại sai - Tổng số: {n_total} ảnh\nBG: Background | BT: Battery_S1 | NM: Normal_S2", fontsize=14, fontweight='bold', y=0.98)
    
    # Gioi han vung ve phu hop va thu hep khoang cach hang hspace cho dep mat
    plt.tight_layout(rect=[0, 0, 1, 0.92])
    plt.subplots_adjust(top=0.88, hspace=0.38)
    
    out_path = os.path.join(EVAL_DIR, 'evaluation_misclassified.png')
    plt.savefig(out_path, dpi=150)
    print(f">>> Da luu bieu do anh sai duy nhat tai: {out_path}")
    plt.close()



def plot_roc_curve(labels, scores, class_names, model_name="Keras (Float32)"):
    # Chuyen doi nhan sang dang One-hot cho viec tinh ROC
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
    # Su dung tieng Anh thuan nhat cho tham so dac trung, khong ghep song ngu va nhan truc khong in dam
    plt.xlabel('False Positive Rate (FPR)', fontsize=12, labelpad=10)
    plt.ylabel('True Positive Rate (TPR)', fontsize=12, labelpad=10)
    plt.title(f'ROC Curve và AUC', fontsize=13, fontweight='bold', pad=15)
    plt.legend(loc="lower right", fontsize=11)
    plt.grid(True, alpha=0.3)
    
    out_path = os.path.join(EVAL_DIR, 'evaluation_roc_curve.png')
    plt.savefig(out_path, bbox_inches='tight')
    print(f">>> Da luu bieu do ROC tai: {out_path}")
    plt.close()

def plot_training_history(history_path):
    print(f"\n>>> Dang ve bieu do lich su huan luyen tu: {history_path}")
    if not os.path.exists(history_path):
        print("Canh bao: Khong tim thay file lich su huan luyen JSON.")
        return

    with open(history_path, "r") as f:
        history = json.load(f)

    # Ghep du lieu tu 2 phase
    acc = history['phase1']['accuracy'] + history['phase2']['accuracy']
    val_acc = history['phase1']['val_accuracy'] + history['phase2']['val_accuracy']
    loss = history['phase1']['loss'] + history['phase2']['loss']
    val_loss = history['phase1']['val_loss'] + history['phase2']['val_loss']
    
    epochs_p1 = len(history['phase1']['accuracy'])
    total_epochs = len(acc)
    
    # Tim epoch tot nhat (dat val_loss nho nhat) de lam moc Best Model
    best_epoch = np.argmin(val_loss)
    best_val_loss = val_loss[best_epoch]
    best_val_acc = val_acc[best_epoch]
    
    # Thuat toan loc ticks truc X thong minh de hien thi dung mong muon nguoi dung
    important_ticks = [epochs_p1 - 1, total_epochs - 1]
        
    default_ticks = list(range(0, total_epochs, 10))
    # Loc bo cac tick chan chuc neu chung qua sat cac tick quan trong (khoang cach <= 2 epochs)
    filtered_default_ticks = []
    for t in default_ticks:
        if all(abs(t - imp) > 2 for imp in important_ticks):
            filtered_default_ticks.append(t)
            
    all_ticks = sorted(list(set(filtered_default_ticks + important_ticks)))
    
    # Ve Accuracy va Loss xep doc (Kich thuoc chuẩn 8x10)
    plt.figure(figsize=(8, 10))
    
    # Ve Accuracy
    plt.subplot(2, 1, 1)
    plt.plot(acc, label='Train Accuracy', color='#1f77b4', linewidth=2)
    plt.plot(val_acc, label='Val Accuracy', color='#ff7f0e', linewidth=2)
    
    # To mau nen nhap nhan cho 2 phase (Khong de label de tranh lam phinh to legend)
    plt.axvspan(0, epochs_p1 - 1, color='#e6f2ff', alpha=0.4)
    plt.axvspan(epochs_p1 - 1, total_epochs - 1, color='#ffe6e6', alpha=0.4)
    
    # Duong ranh gioi giua 2 phase va duong Early Stopped
    plt.axvline(x=epochs_p1-1, color='red', linestyle='--', linewidth=1.5, label='Fine-tuning Start')
    plt.axvline(x=total_epochs-1, color='purple', linestyle=':', linewidth=1.5, label='Early Stopped')
    
    # Danh dau Best Model bang ngoi sao xanh lon (Thong tin chi tiet da co trong legend)
    plt.scatter(best_epoch, best_val_acc, color='#2ca02c', marker='*', s=200, zorder=5, 
                label=f'Best Model: {best_val_acc*100:.2f}% (Epoch {best_epoch})')
    
    plt.title('Accuracy theo Epoch', fontsize=13, fontweight='bold', pad=10)
    plt.xlabel('Epoch')
    plt.ylabel('Accuracy')
    
    # Thiet lap ticks truc X va Legend nho gon nam o goc duoi ben phai
    plt.xticks(all_ticks)
    plt.legend(loc='lower right', fontsize=8, framealpha=0.7)
    plt.grid(True, alpha=0.3)

    # Ve Loss
    plt.subplot(2, 1, 2)
    plt.plot(loss, label='Train Loss', color='#d62728', linewidth=2)
    plt.plot(val_loss, label='Val Loss', color='#2ca02c', linewidth=2)
    
    # To mau nen nhap nhan cho 2 phase (Khong de label de tranh lam phinh to legend)
    plt.axvspan(0, epochs_p1 - 1, color='#e6f2ff', alpha=0.4)
    plt.axvspan(epochs_p1 - 1, total_epochs - 1, color='#ffe6e6', alpha=0.4)
    
    # Duong ranh gioi giua 2 phase va duong Early Stopped
    plt.axvline(x=epochs_p1-1, color='red', linestyle='--', linewidth=1.5, label='Fine-tuning Start')
    plt.axvline(x=total_epochs-1, color='purple', linestyle=':', linewidth=1.5, label='Early Stopped')
    
    # Danh dau Best Model bang ngoi sao do lon (Thong tin chi tiet da co trong legend)
    plt.scatter(best_epoch, best_val_loss, color='#d62728', marker='*', s=200, zorder=5, 
                label=f'Best Model: {best_val_loss:.4f} (Epoch {best_epoch})')
    
    plt.title('Loss theo Epoch', fontsize=13, fontweight='bold', pad=10)
    plt.xlabel('Epoch')
    plt.ylabel('Loss')
    
    # Thiet lap ticks truc X va Legend nho gon nam o goc tren ben phai
    plt.xticks(all_ticks)
    plt.legend(loc='upper right', fontsize=8, framealpha=0.7)
    plt.grid(True, alpha=0.3)

    plt.tight_layout(pad=3.0)
    out_path = os.path.join(EVAL_DIR, 'evaluation_training_history.png')
    plt.savefig(out_path)
    plt.close()
    print(f">>> Da luu bieu do cai tien cuc ky truc quan tai: {out_path}")

def cleanup_old_results(directory):
    if os.path.exists(directory):
        print(f"\n>>> Dang don dep thu muc anh cu tai: {directory}...")
        # Chi don dep cac file do chinh script nay tao ra de tranh xoa nham anh kiem chung verify
        files_to_remove = [
            'evaluation_confusion_matrix.png',
            'evaluation_misclassified.png',
            'evaluation_roc_curve.png',
            'evaluation_training_history.png'
        ]
        for f in files_to_remove:
            file_path = os.path.join(directory, f)
            if os.path.exists(file_path):
                try:
                    os.remove(file_path)
                except Exception as e:
                    print(f"Canh bao: Khong the xoa {f}: {e}")
        print(">>> Don dep hoan tat.")
    else:
        os.makedirs(directory, exist_ok=True)

def main():
    if not os.path.exists(KERAS_MODEL_PATH):
        print(f"Loi: Khong tim thay file model Keras tai ({KERAS_MODEL_PATH})")
        return

    # Xoa anh cu truoc khi chay danh gia moi
    cleanup_old_results(EVAL_DIR)

    images, labels, class_names = load_data()
    if images is None:
        print("Loi: Khong load duoc du lieu.")
        return

    keras_preds, keras_scores, keras_acc = evaluate_keras(KERAS_MODEL_PATH, images, labels)

    # Danh gia va ve bieu do cho Keras (Float32) - Mo hinh goc
    print("\n>>> Bao cao chi tiet (Keras Float32):")
    print(classification_report(labels, keras_preds, target_names=class_names))
    plot_confusion_matrix(labels, keras_preds, class_names, keras_acc, "Keras (Float32)")
    plot_misclassified_images(images, labels, keras_preds, keras_scores, class_names, "Keras (Float32)")
    plot_roc_curve(labels, keras_scores, class_names, "Keras (Float32)")

    # Ve bieu do lich su huan luyen
    plot_training_history(HISTORY_PATH)
    print("\n>>> Danh gia hoan tat!")

if __name__ == "__main__":
    main()
