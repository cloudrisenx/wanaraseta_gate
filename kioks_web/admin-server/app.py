import os
import socket
from flask import Flask, render_template, request, redirect, url_for, session, jsonify, flash
from flask_cors import CORS
from werkzeug.utils import secure_filename
from models import db, KioskConfig, KioskMedia

app = Flask(__name__)
app.secret_key = os.environ.get('SECRET_KEY', 'smart-gate-kiosk-super-secret-key-123')

def is_mqtt_alive(host="10.127.10.8", port=1883, timeout=1):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(timeout)
        s.connect((host, port))
        s.close()
        return True
    except Exception:
        return False


# Enable CORS for Next.js frontend requests
CORS(app, resources={r"/api/*": {"origins": "*"}})

# Configure Database (PostgreSQL with SQLite fallback)
database_url = os.environ.get('DATABASE_URL', 'sqlite:///kiosk.db')
if database_url.startswith("postgres://"):
    # SQLAlchemy requires 'postgresql://' instead of 'postgres://'
    database_url = database_url.replace("postgres://", "postgresql://", 1)

app.config['SQLALCHEMY_DATABASE_URI'] = database_url
app.config['SQLALCHEMY_TRACK_MODIFICATIONS'] = False

# Configure Upload Folder
UPLOAD_FOLDER = os.path.join(app.root_path, 'static', 'uploads')
app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER
app.config['MAX_CONTENT_LENGTH'] = 100 * 1024 * 1024  # 100 MB max limit

# Ensure upload directory exists
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

# Initialize Database
db.init_app(app)
with app.app_context():
    db.create_all()

# File Upload Configuration
ALLOWED_EXTENSIONS = {'png', 'jpg', 'jpeg', 'gif', 'mp4', 'webm', 'ogg'}

def allowed_file(filename):
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS

# Session Authentication Decorator
def login_required(f):
    from functools import wraps
    @wraps(f)
    def decorated_function(*args, **kwargs):
        if not session.get('logged_in'):
            flash('Silakan login terlebih dahulu.', 'error')
            return redirect(url_for('login'))
        return f(*args, **kwargs)
    return decorated_function

# Routes
@app.route('/')
def index():
    if session.get('logged_in'):
        return redirect(url_for('admin_dashboard'))
    return redirect(url_for('login'))

@app.route('/login', methods=['GET', 'POST'])
def login():
    if session.get('logged_in'):
        return redirect(url_for('admin_dashboard'))
        
    if request.method == 'POST':
        username = request.form.get('username')
        password = request.form.get('password')
        
        # Hardcoded credentials as per requirements
        if username == 'admin' and password == 'admin123':
            session['logged_in'] = True
            flash('Login berhasil!', 'success')
            return redirect(url_for('admin_dashboard'))
        else:
            flash('Username atau password salah.', 'error')
            
    return render_template('login.html')

@app.route('/logout')
def logout():
    session.pop('logged_in', None)
    flash('Anda telah logout.', 'info')
    return redirect(url_for('login'))

@app.route('/admin')
@login_required
def admin_dashboard():
    configs = db.session.scalars(db.select(KioskConfig)).all()
    mqtt_online = is_mqtt_alive()
    return render_template('dashboard.html', configs=configs, mqtt_online=mqtt_online)

@app.route('/admin/create', methods=['POST'])
@login_required
def create_kiosk():
    tv_id = request.form.get('tv_id', '').strip()
    mqtt_topic = request.form.get('mqtt_topic', '').strip()
    
    if not tv_id:
        flash('TV ID harus diisi.', 'error')
        return redirect(url_for('admin_dashboard'))
        
    existing = db.session.scalars(db.select(KioskConfig).filter_by(tv_id=tv_id)).first()
    if existing:
        flash(f'TV ID "{tv_id}" sudah terdaftar.', 'error')
        return redirect(url_for('admin_dashboard'))
        
    new_config = KioskConfig(tv_id=tv_id, mqtt_topic=mqtt_topic if mqtt_topic else None)
    db.session.add(new_config)
    db.session.commit()
    flash(f'Kiosk {tv_id} berhasil didaftarkan.', 'success')
    return redirect(url_for('edit_kiosk', tv_id=tv_id))

@app.route('/admin/edit/<tv_id>', methods=['GET', 'POST'])
@login_required
def edit_kiosk(tv_id):
    config = db.first_or_404(db.select(KioskConfig).filter_by(tv_id=tv_id))
    
    if request.method == 'POST':
        mqtt_topic = request.form.get('mqtt_topic', '').strip()
        config.mqtt_topic = mqtt_topic if mqtt_topic else None
        
        # Handle background music file upload
        music_file = request.files.get('music_file')
        if music_file and allowed_file(music_file.filename):
            # Delete old music file from disk if exists
            if config.music_filename:
                old_music_path = os.path.join(app.config['UPLOAD_FOLDER'], config.music_filename)
                if os.path.exists(old_music_path):
                    try:
                        os.remove(old_music_path)
                    except Exception as e:
                        print(f"Error removing old music: {e}")
            
            filename = secure_filename(music_file.filename)
            base, extension = os.path.splitext(filename)
            counter = 1
            unique_filename = filename
            while os.path.exists(os.path.join(app.config['UPLOAD_FOLDER'], unique_filename)):
                unique_filename = f"{base}_{counter}{extension}"
                counter += 1
                
            music_file.save(os.path.join(app.config['UPLOAD_FOLDER'], unique_filename))
            config.music_filename = unique_filename
            
        # Handle multi-file upload for slideshow media
        uploaded_files = request.files.getlist('media_files')
        for file in uploaded_files:
            if file and allowed_file(file.filename):
                filename = secure_filename(file.filename)
                
                # Check for filename collisions and modify name if necessary
                base, extension = os.path.splitext(filename)
                counter = 1
                unique_filename = filename
                while os.path.exists(os.path.join(app.config['UPLOAD_FOLDER'], unique_filename)):
                    unique_filename = f"{base}_{counter}{extension}"
                    counter += 1
                
                file_path = os.path.join(app.config['UPLOAD_FOLDER'], unique_filename)
                file.save(file_path)
                
                # Create Media entry
                new_media = KioskMedia(kiosk_config_id=config.id, media_filename=unique_filename)
                db.session.add(new_media)
                
        db.session.commit()
        flash('Konfigurasi dan media berhasil diperbarui!', 'success')
        return redirect(url_for('edit_kiosk', tv_id=tv_id))
        
    return render_template('edit.html', config=config)

@app.route('/admin/delete/<tv_id>', methods=['POST'])
@login_required
def delete_kiosk(tv_id):
    config = db.first_or_404(db.select(KioskConfig).filter_by(tv_id=tv_id))
    
    # Delete background music file from disk if exists
    if config.music_filename:
        music_path = os.path.join(app.config['UPLOAD_FOLDER'], config.music_filename)
        if os.path.exists(music_path):
            try:
                os.remove(music_path)
            except Exception as e:
                print(f"Error removing music file {music_path}: {e}")
                
    # Delete actual slideshow files on disk linked to this kiosk
    for media in config.media_files:
        file_path = os.path.join(app.config['UPLOAD_FOLDER'], media.media_filename)
        if os.path.exists(file_path):
            try:
                os.remove(file_path)
            except Exception as e:
                print(f"Error removing file {file_path}: {e}")
                
    db.session.delete(config)
    db.session.commit()
    flash(f'Kiosk {tv_id} berhasil dihapus.', 'info')
    return redirect(url_for('admin_dashboard'))

@app.route('/admin/music/delete/<tv_id>', methods=['POST'])
@login_required
def delete_music(tv_id):
    config = db.first_or_404(db.select(KioskConfig).filter_by(tv_id=tv_id))
    
    if config.music_filename:
        file_path = os.path.join(app.config['UPLOAD_FOLDER'], config.music_filename)
        if os.path.exists(file_path):
            try:
                os.remove(file_path)
            except Exception as e:
                print(f"Error deleting music file {file_path}: {e}")
                
        config.music_filename = None
        db.session.commit()
        flash('Musik latar belakang berhasil dihapus.', 'success')
    else:
        flash('Tidak ada musik latar belakang untuk dihapus.', 'info')
        
    return redirect(url_for('edit_kiosk', tv_id=tv_id))

@app.route('/admin/media/delete/<int:media_id>', methods=['POST'])
@login_required
def delete_media(media_id):
    media = db.get_or_404(KioskMedia, media_id)
    config = db.session.get(KioskConfig, media.kiosk_config_id)
    
    # Remove file from disk
    file_path = os.path.join(app.config['UPLOAD_FOLDER'], media.media_filename)
    if os.path.exists(file_path):
        try:
            os.remove(file_path)
        except Exception as e:
            print(f"Error deleting media file {file_path}: {e}")
            
    db.session.delete(media)
    db.session.commit()
    flash('Media berhasil dihapus.', 'success')
    return redirect(url_for('edit_kiosk', tv_id=config.tv_id))

# Kiosk public JSON API
@app.route('/api/config/<tv_id>', methods=['GET'])
def api_config(tv_id):
    config = db.session.scalars(db.select(KioskConfig).filter_by(tv_id=tv_id)).first()
    if not config:
        return jsonify({
            "error": "TV ID not found",
            "tv_id": tv_id,
            "mqtt_topic": "",
            "media_urls": []
        }), 404
        
    # Get base URL to construct absolute links
    base_url = request.host_url
    return jsonify(config.to_dict(base_url))

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
