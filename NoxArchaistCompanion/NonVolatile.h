#pragma once
class NonVolatile
{
public:
	std::wstring profilePath;
	std::wstring hdvPath;
	int volumeSpeaker;
	int volumeMockingBoard;
	bool useGameLink;

	// I/O
	int SaveToDisk();
	int LoadFromDisk();
};

