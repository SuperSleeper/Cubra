#pragma once

#include <QString>
#include <QStringList>

#include <vector>

namespace td {

// The how-to manual (src/ai/manual.md, built into the app), which the AI assistant
// answers questions from and the on-screen walkthrough follows.
struct ManualStep {
    QString text;
    QString target;  // the objectName the walkthrough points at, "viewport", or empty
};

struct ManualTopic {
    QString id;
    QString title;
    QStringList keywords;
    QStringList also;  // interface names covered in the text rather than by a step
    QString summary;   // the paragraph under the title
    std::vector<ManualStep> steps;
    QStringList tips;

    // Every interface name the topic covers: step targets and "also" names.
    QStringList targets() const;
    // The topic as plain text, for the model's prompt.
    QString text() const;
};

// Parses manual text. Topics missing an id or title are skipped.
std::vector<ManualTopic> parseManual(const QString& markdown);

// The built-in manual, parsed once.
const std::vector<ManualTopic>& manual();
const ManualTopic* manualTopic(const QString& id);

// The topics that best match a question, best first, at most count of them. Topics
// that share no words with the question are left out.
std::vector<const ManualTopic*> findTopics(const QString& question, int count, const std::vector<ManualTopic>& topics = manual());

// Whether a request is a question about using Cubra ("how do I round an edge?") rather
// than something to design ("make a cup", "can you make it taller?").
bool isHelpQuestion(const QString& request);

}  // namespace td
