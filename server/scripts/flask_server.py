from flask import Flask, request, jsonify
from flask_cors import CORS
import mysql.connector
from datetime import datetime, timedelta
import jwt
from flask_jwt_extended import JWTManager, jwt_required, get_jwt_identity, create_access_token

app = Flask(__name__)
CORS(app)

# Database configuration - change to your actual values
DB_CONFIG = {
    'host': 'localhost',
    'user': 'smarthome',
    'password': '123root456maria',
    'database': 'smarthome_db'
}

# Secret key for JWT - change this to a strong random string
app.config['JWT_SECRET_KEY'] = 'x7kP9mQzL2vR8tW4yB6nJ3fH5gD0cA1eUoI2sM4pN6rT8vY0wX9zQ2kJ5lB7mF3'
jwt = JWTManager(app)

# Hardcoded user for testing (later move to DB)
USERS = {
    "test@example.com": "123456"
}


def get_db_connection():
    """Create and return a new database connection"""
    return mysql.connector.connect(**DB_CONFIG)


@app.route('/login', methods=['POST'])
def login():
    """Handle user login and return JWT access token"""
    data = request.get_json(silent=True)
    if not data:
        return jsonify({"error": "Invalid JSON"}), 400

    email = data.get('email')
    password = data.get('password')

    if email in USERS and USERS[email] == password:
        # Create access token using flask-jwt-extended
        access_token = create_access_token(identity=email)
        return jsonify({"token": access_token}), 200

    return jsonify({"error": "Invalid email or password"}), 401

@app.route('/status', methods=['GET'])
@jwt_required()
def get_status():
    """Return current sensor data and security status from database"""
    try:
        conn = get_db_connection()
        cursor = conn.cursor(dictionary=True)

        # Get latest sensor readings
        cursor.execute("""
            SELECT temperature, humidity, pressure 
            FROM sensors 
            ORDER BY timestamp DESC 
            LIMIT 1
        """)
        sensors = cursor.fetchone() or {}

        # Get current security status (single row)
        cursor.execute("""
            SELECT motion, door, face, lock_status, updated 
            FROM security_status 
            WHERE id = 1
        """)
        security = cursor.fetchone() or {}

        cursor.close()
        conn.close()

        # Build response
        response = {
            "temperature": sensors.get('temperature'),
            "humidity": sensors.get('humidity'),
            "pressure": sensors.get('pressure'),
            "door": security.get('door', 'closed'),
            "motion_detected": security.get('motion') == 'DETECTED',
            "last_access": security.get('updated'),
            "face_recognition": security.get('face'),
            "lock_status": security.get('lock_status')
        }

        return jsonify(response), 200

    except Exception as e:
        return jsonify({"error": str(e)}), 500


if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=True)
