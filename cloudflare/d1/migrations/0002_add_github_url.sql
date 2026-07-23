ALTER TABLE releases ADD COLUMN github_url TEXT;
ALTER TABLE releases ADD COLUMN github_owner TEXT;
ALTER TABLE releases ADD COLUMN github_repo TEXT;

UPDATE releases
SET github_owner = 'vayvayyarram-creator',
    github_repo  = 'CrashSp',
    github_url   = 'https://github.com/vayvayyarram-creator/CrashSp/releases/download/v1.0.0/SpCrashReport.dll'
WHERE version = 'v1.0.0' AND github_url IS NULL;
