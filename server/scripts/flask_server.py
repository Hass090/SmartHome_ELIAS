from flask import Flask, request, jsonify
from flask_cors import CORS
from flask_jwt_extended import JWTManager, jwt_required, create_access_token, get_jwt_identity
import mysql.connector
from mysql.connector import Error
from datetime import datetime, timedelta
import logging

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.StreamHandler(),
        logging.FileHandler('server.log')
    ]
)
logger = logging.getLogger(__name__)

app = Flask(__name__)
CORS(app)

# JWT configuration
app.config['JWT_SECRET_KEY'] = 'x7kP9mQzL2vR8tW4yB6nJ3fH5gD0cA1eUoI2sM4pN6rT8vY0wX9zQ2kJ5lB7mF3'
app.config['JWT_ACCESS_TOKEN_EXPIRES'] = timedelta(hours=24)
jwt = JWTManager(app)

# Initialize Firebase Admin SDK for push notifications
import firebase_admin
from firebase_admin import credentials, messaging
cred = credentials.Certificate('/home/hass/smarthomeapp-elias-firebase-adminsdk-fbsvc-d5527a5fbc.json')
firebase_admin.initialize_app(cred)

# Database configuration
DB_CONFIG = {
    'host': 'localhost',
    'user': 'smarthome',
    'password': '123root456maria',
    'database': 'smarthome_db',
    'raise_on_warnings': True
}

def get_db_connection():
    """Create and return a new database connection"""
    try:
        conn = mysql.connector.connect(**DB_CONFIG)
        return conn
    except Error as e:
        logger.error(f"Database connection failed: {e}")
        raise

@app.route('/login', methods=['POST'])
def login():
    """Authenticate user and return JWT access token"""
    try:
        data = request.get_json(silent=True)
        if not data:
            return jsonify({"error": "Invalid JSON"}), 400
        email = data.get('email')
        password = data.get('password')
        if not email or not password:
            return jsonify({"error": "Email and password required"}), 400

        conn = get_db_connection()
        cursor = conn.cursor(dictionary=True)
        cursor.execute(
            "SELECT email FROM users WHERE email = %s AND password_hash = %s",
            (email, password)
        )
        user = cursor.fetchone()
        cursor.close()
        conn.close()

        if user:
            access_token = create_access_token(identity=email)
            logger.info(f"Successful login: {email}")
            return jsonify({"token": access_token}), 200

        logger.warning(f"Failed login attempt: {email}")
        return jsonify({"error": "Invalid email or password"}), 401
    except Error as e:
        logger.error(f"Database error during login: {e}")
        return jsonify({"error": "Database error"}), 500
    except Exception as e:
        logger.error(f"Unexpected error in login: {e}")
        return jsonify({"error": "Internal server error"}), 500

@app.route('/status', methods=['GET'])
@jwt_required()
def get_status():
    """Return current sensor readings and security status"""
    try:
        conn = get_db_connection()
        cursor = conn.cursor(dictionary=True)
        cursor.execute("""
            SELECT temperature, humidity, pressure
            FROM sensors
            ORDER BY timestamp DESC
            LIMIT 1
        """)
        sensors = cursor.fetchone() or {}
        cursor.execute("""
            SELECT motion, door, face, lock_status, updated
            FROM security_status
            WHERE id = 1
        """)
        security = cursor.fetchone() or {}
        cursor.close()
        conn.close()

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
    except Error as e:
        logger.error(f"Database error in /status: {e}")
        return jsonify({"error": "Database error"}), 500
    except Exception as e:
        logger.error(f"Unexpected error in /status: {e}")
        return jsonify({"error": "Internal server error"}), 500

@app.route('/history', methods=['GET'])
@jwt_required()
def get_history():
    """Return event history with optional filters"""
    try:
        limit = request.args.get('limit', default=50, type=int)
        offset = request.args.get('offset', default=0, type=int)
        event_type = request.args.get('type', default=None, type=str)

        conn = get_db_connection()
        cursor = conn.cursor(dictionary=True)
        query = "SELECT timestamp, type, message FROM events ORDER BY timestamp DESC LIMIT %s OFFSET %s"
        params = [limit, offset]
        if event_type and event_type != 'all':
            query = "SELECT timestamp, type, message FROM events WHERE type = %s ORDER BY timestamp DESC LIMIT %s OFFSET %s"
            params = [event_type, limit, offset]
        cursor.execute(query, params)
        events = cursor.fetchall()
        for event in events:
            event['timestamp'] = event['timestamp'].strftime('%Y-%m-%d %H:%M:%S')
        cursor.close()
        conn.close()
        return jsonify(events), 200
    except Error as e:
        logger.error(f"Database error in /history: {e}")
        return jsonify({"error": "Database error"}), 500
    except Exception as e:
        logger.error(f"Unexpected error in /history: {e}")
        return jsonify({"error": "Internal server error"}), 500

@app.route('/register_token', methods=['POST'])
@jwt_required()
def register_token():
    """Register or update FCM token (only 1 token per user)"""
    try:
        data = request.get_json(silent=True)
        if not data:
            logger.error("register_token: Invalid JSON")
            return jsonify({"error": "Invalid JSON"}), 400
        fcm_token = data.get('fcm_token')
        if not fcm_token:
            logger.error("register_token: FCM token required")
            return jsonify({"error": "FCM token required"}), 400

        user_email = get_jwt_identity()
        device_id = data.get('device_id', 'default_flutter_device')

        logger.info(f"register_token: Starting for user {user_email}, token length: {len(fcm_token)} chars")

        conn = get_db_connection()
        cursor = conn.cursor(dictionary=True)

        deleted = cursor.execute("DELETE FROM push_tokens WHERE user_email = %s", (user_email,))
        logger.info(f"register_token: Deleted {cursor.rowcount} old tokens for {user_email}")

        cursor.execute("""
            INSERT INTO push_tokens (user_email, fcm_token, device_id, updated_at)
            VALUES (%s, %s, %s, NOW())
        """, (user_email, fcm_token, device_id))
        conn.commit()
        cursor.close()
        conn.close()

        logger.info(f"register_token: SUCCESS - New FCM token saved for {user_email}")
        return jsonify({"status": "Token registered successfully"}), 200
    except Error as e:
        logger.error(f"Database error in /register_token: {e}")
        return jsonify({"error": "Database error"}), 500
    except Exception as e:
        logger.error(f"Unexpected error in /register_token: {e}")
        return jsonify({"error": "Internal server error"}), 500

@app.route('/test_push', methods=['POST'])
@jwt_required()
def test_push():
    """Test push for verification"""
    try:
        user_email = get_jwt_identity()
        send_push_to_user(user_email, "Test from SmartHome", "Push notifications are working! 🎉 Door/motion events coming soon")
        return jsonify({"status": "Test push sent successfully"}), 200
    except Exception as e:
        logger.error(f"Test push error: {e}")
        return jsonify({"error": str(e)}), 500

def send_push_to_user(user_email, title, body):
    """Send push notification to all registered devices of a user"""
    try:
        conn = get_db_connection()
        cursor = conn.cursor(dictionary=True)
        cursor.execute("SELECT fcm_token FROM push_tokens WHERE user_email = %s", (user_email,))
        tokens = [row['fcm_token'] for row in cursor.fetchall()]
        cursor.close()
        conn.close()

        if not tokens:
            logger.warning(f"No FCM tokens found for user {user_email}")
            return

        message = messaging.MulticastMessage(
            notification=messaging.Notification(
                title=title,
                body=body,
            ),
            tokens=tokens,
            data={
                'type': 'event',
                'timestamp': datetime.now().isoformat()
            }
        )
        response = messaging.send_each_for_multicast(message)
        logger.info(f"Push sent to {user_email}: {response.success_count} success, {response.failure_count} failed")
    except Exception as e:
        logger.error(f"Failed to send push to {user_email}: {e}")

if __name__ == '__main__':
    logger.info("Starting SmartHome Flask server...")
    app.run(host='0.0.0.0', port=5000, debug=True)