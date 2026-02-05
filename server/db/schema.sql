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
