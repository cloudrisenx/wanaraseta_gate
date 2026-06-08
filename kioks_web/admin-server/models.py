from flask_sqlalchemy import SQLAlchemy

db = SQLAlchemy()

class KioskConfig(db.Model):
    __tablename__ = 'kiosk_config'
    __allow_unmapped__ = True
    
    # Explicit type hints for VS Code/Pylance static analyzer
    id: int
    tv_id: str
    mqtt_topic: str | None
    music_filename: str | None
    media_files: list['KioskMedia']
    
    id = db.Column(db.Integer, primary_key=True)
    tv_id = db.Column(db.String(80), unique=True, nullable=False, index=True)
    mqtt_topic = db.Column(db.String(255), nullable=True)
    music_filename = db.Column(db.String(255), nullable=True)
    
    # Establish relationship with cascade delete
    media_files = db.relationship('KioskMedia', backref='kiosk', lazy=True, cascade="all, delete-orphan")

    # Explicit constructor so Pylance knows valid arguments
    def __init__(self, tv_id: str, mqtt_topic: str | None = None, music_filename: str | None = None):
        self.tv_id = tv_id
        self.mqtt_topic = mqtt_topic
        self.music_filename = music_filename

    def to_dict(self, base_url: str):
        return {
            "tv_id": self.tv_id,
            "mqtt_topic": self.mqtt_topic,
            "music_url": f"{base_url.rstrip('/')}/static/uploads/{self.music_filename}" if self.music_filename else None,
            "media_urls": [f"{base_url.rstrip('/')}/static/uploads/{media.media_filename}" for media in self.media_files]
        }

class KioskMedia(db.Model):
    __tablename__ = 'kiosk_media'
    __allow_unmapped__ = True
    
    # Explicit type hints for VS Code/Pylance static analyzer
    id: int
    kiosk_config_id: int
    media_filename: str
    
    id = db.Column(db.Integer, primary_key=True)
    kiosk_config_id = db.Column(db.Integer, db.ForeignKey('kiosk_config.id', ondelete='CASCADE'), nullable=False)
    media_filename = db.Column(db.String(255), nullable=False)

    # Explicit constructor so Pylance knows valid arguments
    def __init__(self, kiosk_config_id: int, media_filename: str):
        self.kiosk_config_id = kiosk_config_id
        self.media_filename = media_filename
