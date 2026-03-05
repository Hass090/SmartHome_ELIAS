USE smarthome_db;

DROP TABLE IF EXISTS sensors;
CREATE TABLE sensors (
    id INT AUTO_INCREMENT PRIMARY KEY,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    temperature FLOAT,
    humidity FLOAT,
    pressure FLOAT,
    INDEX idx_timestamp (timestamp)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS security_status;
CREATE TABLE security_status (
    id INT PRIMARY KEY DEFAULT 1,
    motion VARCHAR(20) DEFAULT 'quiet',
    door VARCHAR(20) DEFAULT 'closed',
    face VARCHAR(20) DEFAULT 'none',
    lock_status VARCHAR(20) DEFAULT 'locked',
    updated DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CHECK (id = 1)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT IGNORE INTO security_status (id) VALUES (1);

DROP TABLE IF EXISTS access_logs;
CREATE TABLE access_logs (
    id INT AUTO_INCREMENT PRIMARY KEY,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    method VARCHAR(30),
    result VARCHAR(20),
    source VARCHAR(30) DEFAULT 'pico'
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

DROP TABLE IF EXISTS events;
CREATE TABLE events (
    id INT AUTO_INCREMENT PRIMARY KEY,
    timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
    type VARCHAR(30),
    message VARCHAR(255)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO events (type, message) VALUES ('system', 'Database schema reloaded');

DROP TABLE IF EXISTS users;
CREATE TABLE users (
    id INT AUTO_INCREMENT PRIMARY KEY,
    email VARCHAR(255) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO users (email, password_hash) 
VALUES ('test@example.com', '123456'),
       ('elias@example.com', 'password123');

DROP TABLE IF EXISTS push_tokens;
CREATE TABLE IF NOT EXISTS push_tokens (
    id INT AUTO_INCREMENT PRIMARY KEY,
    user_email VARCHAR(255) NOT NULL,
    fcm_token TEXT NOT NULL,
    device_id VARCHAR(100) DEFAULT 'default',
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY unique_token_per_user_device (user_email, device_id)
);