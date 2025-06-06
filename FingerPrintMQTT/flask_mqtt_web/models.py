from flask_sqlalchemy import SQLAlchemy

db = SQLAlchemy()

class Deteccion(db.Model):
    id = db.Column(db.Integer, primary_key=True)
    huella_id = db.Column(db.Integer, nullable=False)
    fecha = db.Column(db.DateTime, nullable=False)

    def __repr__(self):
        return f"<Deteccion huella_id={self.huella_id} fecha={self.fecha}>"
