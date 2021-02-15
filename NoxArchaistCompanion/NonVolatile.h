#pragma once
class NonVolatile
{
public:
	std::wstring profilePath;
	std::wstring hdvPath;
	int volumeSpeaker = 0;
	int volumeMockingBoard = 0;
	bool useGameLink = false;

	// I/O
	int SaveToDisk();
	int LoadFromDisk();
};

